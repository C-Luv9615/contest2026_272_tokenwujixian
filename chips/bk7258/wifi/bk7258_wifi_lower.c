/*
 * chips/bk7258/wifi/bk7258_wifi_lower.c
 *
 * NuttX netdev_lowerhalf data/control plane for BK7258 Wi-Fi (STA MVP).
 *
 * Skeleton status: the ops tables and register/carrier plumbing are wired;
 * transmit/receive and the wireless_ops_s handlers return documented
 * errors until the Beken public API (bk_wifi_sta_*, bk_wifi_scan_*) and the
 * vendor MAC TX/RX entry points are connected by the integration branch.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/wireless/wireless.h>

/* getreg32 is a NuttX macro here (arm_internal.h), not a function; the
 * scan-done diagnostics read the NX MAC FSM registers directly. */
#include <arm_internal.h>
#include <bk7258_memorymap.h>

#include <errno.h>
#include <net/if_arp.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

#include "lwip/pbuf.h"
#include "generated/lmac_bus_msg.h"
#include "rwnx_intf.h"
#include "bk_private/bk_wifi.h"
#include "common/bk_err.h"

#include "generated/lmac_wifi_adapter.h"

#include "bk7258_wifi_internal.h"
#include "bk7258_scan_diag.h"
/* up_udelay: the clktick sample needs a real interval between the two
 * reads, not two adjacent loads. */
#include <nuttx/arch.h>

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
#include <components/event.h>
#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include "bk_phy_adapter.h"
#include "bk_rf_adapter.h"
#include <syslog.h>

/* ke_l2_packet_tx() and struct ke_sk_params, the supplicant-side entry. */

#include <wpa_compat/sk_intf.h>

/* Ethernet header layout for the EAPOL split in ethernetif_input().
 *
 * Spelled out locally instead of including "lwip/prot/ethernet.h": that header
 * pulls in lwip/prot/ieee.h, which declares ETHTYPE_* as enum members, while
 * NuttX's nuttx/net/ethernet.h:55-56 defines ETHTYPE_ARP and ETHTYPE_IP as
 * macros -- and that header is already in scope here through
 * nuttx/net/netdev.h.  The macros then expand inside the enum
 * ("ETHERTYPE_ARP = 0x0806u") and the whole enum fails to parse.  The vendored
 * glue can use the lwIP header only because it never includes NuttX net
 * headers.
 *
 * Reading the two ethertype bytes by hand also keeps the comparison in network
 * byte order, so no htons() and no <arpa/inet.h> are needed, and it does not
 * assume the payload is 2-byte aligned.  ETH_PAD_SIZE is 0 in this port, so the
 * 802.3 header is exactly dest[6] + src[6] + type[2].
 */

#define BK7258_ETH_HDR_LEN      14u
#define BK7258_ETH_SRC_OFFSET    6u
#define BK7258_ETH_TYPE_OFFSET  12u
#define BK7258_ETHTYPE_EAPOL    0x888eu
#endif

extern int bmsg_tx_sender(struct pbuf *p, uint32_t vif_idx);

/* os/os.h defines this as beken_thread_t *; beken_thread_t is void *. Keep
 * the declaration local so this NuttX lower-half does not import its broad
 * compatibility macro surface merely to obtain the SDK scan request token. */

extern void **rtos_get_current_thread(void);
extern uint32_t bk7258_wifi_pwd_ofdm_get_override(void);

/****************************************************************************
 * Private data
 ****************************************************************************/

static struct bk7258_wifi_s g_bk7258_wifi;

#define PRIV2LOWER(p) (&(p)->lower)

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static bk_err_t bk7258_wifi_scan_done(void *arg, event_module_t module,
                                      int event_id, void *event_data);
#endif

void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt);

static int bk7258_wifi_rx_enqueue(struct bk7258_wifi_s *priv,
                                  struct iob_s *iob)
{
  irqstate_t flags;

  flags = enter_critical_section();
  iob->io_flink = NULL;
  if (priv->rx_tail != NULL)
    {
      priv->rx_tail->io_flink = iob;
    }
  else
    {
      priv->rx_head = iob;
    }

  priv->rx_tail = iob;
  leave_critical_section(flags);
  return 0;
}

/* Armino's connector callback for an Ethernet frame. The vendor pbuf is
 * copied into the NuttX queue and released only after the copy completes.
 *
 * EAPOL is split off before that copy, see the block below.
 */

void ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx)
{
  struct bk7258_vpkt *vpkt;
  struct pbuf *q;
  uint8_t *dst;
  (void)iface;
  (void)dst_idx;

  if (p == NULL || p->tot_len == 0 || p->tot_len > BK7258_WIFI_FRAME_MAX)
    {
      if (p != NULL)
        {
          pbuf_free(p);
        }
      return;
    }

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  /* Hand EAPOL to wpa_supplicant instead of to the NuttX stack.
   *
   * Transcribed from the vendor's own bridge, ethernetif_input() in
   * cp/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c:331 -- there the
   * EAPOL test is likewise the first thing done to a frame, ahead of the netif
   * lookup, with this comment: "EAPOL must reach wpa_supplicant even when lwip
   * netif is not registered yet".
   *
   * Why it has to be here: the 4-way handshake is carried in 802.3 frames with
   * ethertype 0x888E, and every 802.3 frame arrives through this one function
   * (rwnx_rx.c:728, the tail of rwm_upload_data(), which is registered as
   * g_rwnx_connector.data_outbound_func in rw_ieee80211.c).  Without this
   * split, M1 was copied into the NuttX RX queue, where the network stack has
   * no consumer for 0x888E and drops it: wpa_supplicant reached ASSOCIATED,
   * armed its 10 s auth timeout, never saw M1, and the AP disassociated us
   * with reason 15 (4WAY_HANDSHAKE_TIMEOUT) about four seconds later.  Board-
   * verified three times per run in the sta-hwioctlfix image.
   *
   * Management frames were never affected, which is why auth/assoc/beacon all
   * worked: those take the separate rwnx_rx_mgmt_any() path.
   *
   * This gap was known and recorded, not newly discovered:
   * glue/include/wpa_compat/sk_intf.h:20-22 states that "EAPOL/WAI (0x888e)
   * demux to the WPA entity is the network bridge's job (plan 11.4.2), not this
   * header's" -- the bridge is this function, and the work was never done.
   *
   * The self-echo filter is the vendor's too: our own transmitted EAPOL can be
   * looped back by the MAC, and feeding it to the supplicant would corrupt the
   * handshake state machine.  Frames whose source is our own address are
   * dropped rather than forwarded.
   *
   * Runtime-gated because ke_l2_packet_tx() lives in sk_intf.c, which is only
   * compiled under CONFIG_BK7258_WIFI_VENDOR_RUNTIME (CMakeLists.txt:158); the
   * runtime-disabled CP image must not reference it.
   */

  if (p->len > BK7258_ETH_HDR_LEN)
    {
      FAR const uint8_t *eth = (FAR const uint8_t *)p->payload;
      uint16_t ethtype = ((uint16_t)eth[BK7258_ETH_TYPE_OFFSET] << 8) |
                          (uint16_t)eth[BK7258_ETH_TYPE_OFFSET + 1u];

      if (ethtype == BK7258_ETHTYPE_EAPOL)
        {
          struct ke_sk_params params;
          uint8_t own_mac[6];

          /* The vendor bridge compares the frame's source against the vif's
           * in-RAM MAC (wlanif.c: wifi_netif_vif_to_mac(vif)).  Our equivalent
           * is the vendor's own accessor: it resolves to bk_get_mac(), which
           * caches the address in RAM after one flash read at first use
           * (hal_port_mac.c:126-137) -- so this stays cheap on the RX path.
           * bk7258_wifi_board_get_mac() was rejected for exactly that reason:
           * it re-reads the flash partition on every call, here inside the
           * core thread during the 4-way window. */

          if (bk_wifi_sta_get_mac(own_mac) == BK_OK &&
              memcmp(own_mac, eth + BK7258_ETH_SRC_OFFSET,
                     sizeof(own_mac)) == 0)
            {
              pbuf_free(p);
              return;
            }

          /* buf/len describe the whole 802.3 frame, header included, exactly
           * as the vendor bridge passes it. */

          params.buf  = (unsigned char *)p->payload;
          params.len  = p->len;
          params.flag = iface;
          params.freq = 0;

          ke_l2_packet_tx(&params);
          pbuf_free(p);
          return;
        }
    }
#endif /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

  vpkt = bk7258_vpkt_alloc(p->tot_len, true);
  if (vpkt == NULL)
    {
      pbuf_free(p);
      return;
    }

  dst = vpkt->payload;
  for (q = p; q != NULL; q = q->next)
    {
      memcpy(dst, q->payload, q->len);
      dst += q->len;
    }
  bk7258_wifi_lower_rx_submit(&g_bk7258_wifi, vpkt);
  pbuf_free(p);
}

/****************************************************************************
 * Data plane
 *
 * transmit/receive stay disconnected until the vendor MAC TX/RX entry points
 * are wired. Returning -ENOSYS from transmit makes the upper half recycle the
 * NetPKT; returning NULL from receive signals "no packet available".
 ****************************************************************************/

static int bk7258_wifi_ifup(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  /* Skeleton: administrative up only. Carrier stays off until the vendor
   * authentication event is connected. */
  priv->carrier = false;
  return OK;
}

static int bk7258_wifi_ifdown(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  priv->carrier = false;
  return OK;
}

static int bk7258_wifi_transmit(FAR struct netdev_lowerhalf_s *lower,
                                FAR netpkt_t *pkt)
{
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  struct pbuf *p;
  unsigned int len;
  int ret;

  len = netpkt_getdatalen(lower, pkt);
  if (len == 0 || len > BK7258_WIFI_FRAME_MAX)
    {
      return -EMSGSIZE;
    }

  p = pbuf_alloc(PBUF_RAW_TX, (u16_t)len, PBUF_RAM);
  if (p == NULL)
    {
      return -ENOMEM;
    }

  ret = netpkt_copyout(lower, p->payload, pkt, len, 0);
  if (ret < 0)
    {
      pbuf_free(p);
      return ret;
    }

  ret = bmsg_tx_sender(p, priv->vif_idx);
  /* bmsg_tx_sender takes and later releases its queue reference. The caller
   * retains the original pbuf reference and must release it in both paths. */
  pbuf_free(p);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  netpkt_free(lower, pkt, NETPKT_TX);
  netdev_lower_txdone(lower);
  return OK;
}

static FAR netpkt_t *bk7258_wifi_receive(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  FAR struct iob_s *iob;
  irqstate_t flags = enter_critical_section();

  iob = priv->rx_head;
  if (iob != NULL)
    {
      priv->rx_head = iob->io_flink;
      if (priv->rx_head == NULL)
        {
          priv->rx_tail = NULL;
        }
      iob->io_flink = NULL;
    }

  leave_critical_section(flags);
  return iob;
}

static void bk7258_wifi_reclaim(FAR struct netdev_lowerhalf_s *lower)
{
  /* Nothing queued at skeleton stage; a real TX queue would walk and free
   * completed descriptors here. */
}

/****************************************************************************
 * Control plane
 *
 * The scan handler is intentionally the only live STA control operation in
 * this increment. It invokes the real asynchronous SDK scan API and exports
 * a NuttX-owned snapshot through WEXT. Association, credentials, WPA and IP
 * configuration remain unavailable until their own event/data-plane contracts
 * are implemented.
 ****************************************************************************/

static int bk7258_wifi_connect(FAR struct netdev_lowerhalf_s *lower)
{
  return -ENOSYS;
}

static int bk7258_wifi_disconnect(FAR struct netdev_lowerhalf_s *lower)
{
  return -ENOSYS;
}

static int bk7258_wifi_essid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  size_t len;

  /* Scan-SSID selection only.  This does NOT associate: the STA-only
   * bring-up contract forbids submitting credentials or associating before
   * scan discovers an AP, so SIOCSIWESSID here records which SSID the next
   * scan should probe for and nothing else.
   *
   * NuttX has no struct iw_scan_req, so a directed scan is expressed the
   * standard WEXT way: SIOCSIWESSID followed by SIOCSIWSCAN. */

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  if (!set)
    {
      len = priv->scan_ssid_len;
      if (iwr->u.essid.pointer == NULL || iwr->u.essid.length < len)
        {
          return -EINVAL;
        }

      memcpy(iwr->u.essid.pointer, priv->scan_ssid, len);
      iwr->u.essid.length = (uint16_t)len;
      iwr->u.essid.flags = len > 0u ? 1u : 0u;
      return OK;
    }

  /* flags == 0 means "any SSID" -> clear back to a broadcast scan. */

  if (iwr->u.essid.flags == 0u || iwr->u.essid.pointer == NULL)
    {
      priv->scan_ssid_len = 0u;
      priv->scan_ssid[0] = '\0';
      return OK;
    }

  len = iwr->u.essid.length;

  /* Some WEXT callers include the terminator in length; drop it. */

  if (len > 0u && ((FAR const char *)iwr->u.essid.pointer)[len - 1u] == '\0')
    {
      len--;
    }

  if (len > BK7258_WIFI_SCAN_SSID_LEN)
    {
      return -EINVAL;
    }

  memcpy(priv->scan_ssid, iwr->u.essid.pointer, len);
  priv->scan_ssid[len] = '\0';
  priv->scan_ssid_len = (uint8_t)len;
  return OK;
#else
  return -ENOSYS;
#endif
}

static int bk7258_wifi_bssid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_passwd(FAR struct netdev_lowerhalf_s *lower,
                              FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_mode(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_auth(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_country(FAR struct netdev_lowerhalf_s *lower,
                               FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_scan(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  FAR struct bk7258_wifi_s *priv = (FAR struct bk7258_wifi_s *)lower;
  size_t required = 0;
  size_t ssid_len;
  uint8_t index;
  int ret;

  if (!priv->scan_lock_ready)
    {
      return -ENODEV;
    }

  if (set)
    {
      nxmutex_lock(&priv->scan_lock);
      if (priv->scan_in_progress)
        {
          nxmutex_unlock(&priv->scan_lock);
          return -EBUSY;
        }

      /* bk_wifi_scan_start() records this exact caller token in its scan
       * request, then returns it in wifi_event_scan_done_t.scan_id. */

      priv->scan_id = (uint32_t)(uintptr_t)rtos_get_current_thread();
      priv->scan_in_progress = true;
      priv->scan_complete = false;
      priv->scan_status = -EAGAIN;
      priv->scan_count = 0;
      nxmutex_unlock(&priv->scan_lock);

      bk7258_scan_diag_begin();

      /* Directed vs broadcast scan.  The authority reference run was a
       * DIRECTED scan (`scanu_start_req ... ssid_len=3`) while ours has always
       * been broadcast (`ssid_len=0`); aligning that removes one difference
       * from the comparison.  Config built exactly like the vendor's own
       * callers do (zeroed struct, ssid copied, everything else left 0). */

      if (priv->scan_ssid_len > 0u)
        {
          wifi_scan_config_t cfg;

          memset(&cfg, 0, sizeof(cfg));
          memcpy(cfg.ssid, priv->scan_ssid, priv->scan_ssid_len);
          syslog(LOG_INFO, "[BK7258-WIFI] scan: directed len=%u\n",
                 (unsigned)priv->scan_ssid_len);
          ret = bk_wifi_scan_start(&cfg);
        }
      else
        {
          ret = bk_wifi_scan_start(NULL);
        }

      if (ret != BK_OK)
        {
          bk7258_scan_diag_record(BK7258_SCAN_DIAG_START_REJECTED);
          nxmutex_lock(&priv->scan_lock);
          priv->scan_in_progress = false;
          priv->scan_complete = true;
          priv->scan_status = ret == BK_ERR_BUSY ? -EBUSY : -EIO;
          nxmutex_unlock(&priv->scan_lock);
          return priv->scan_status;
        }

      bk7258_scan_diag_record(BK7258_SCAN_DIAG_START_ACCEPTED);

      return OK;
    }

  if (iwr == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->scan_lock);
  if (priv->scan_in_progress)
    {
      nxmutex_unlock(&priv->scan_lock);
      return -EAGAIN;
    }

  if (!priv->scan_complete)
    {
      nxmutex_unlock(&priv->scan_lock);
      return -EINVAL;
    }

  if (priv->scan_status < 0)
    {
      ret = priv->scan_status;
      nxmutex_unlock(&priv->scan_lock);
      return ret;
    }

  /* Calculate the full WEXT event stream before writing user storage. This
   * avoids returning partial results or exposing an incomplete AP record. */

  for (index = 0; index < priv->scan_count; index++)
    {
      ssid_len = strnlen(priv->scan_aps[index].ssid,
                         BK7258_WIFI_SCAN_SSID_LEN);
      required += IW_EV_LEN(ap_addr) + IW_EV_LEN(qual) + IW_EV_LEN(freq) +
                  IW_EV_LEN(data) + IW_EV_LEN(essid) +
                  ((ssid_len + 3u) & ~3u);
    }

  if (required > UINT16_MAX || iwr->u.data.pointer == NULL ||
      required > iwr->u.data.length)
    {
      iwr->u.data.length = required > UINT16_MAX ? UINT16_MAX : required;
      nxmutex_unlock(&priv->scan_lock);
      return -E2BIG;
    }

  {
    FAR uint8_t *cursor = iwr->u.data.pointer;

    for (index = 0; index < priv->scan_count; index++)
      {
        FAR struct iw_event *iwe = (FAR struct iw_event *)cursor;
        FAR struct bk7258_wifi_scan_ap_s *ap = &priv->scan_aps[index];
        iwe->cmd = SIOCGIWAP;
        iwe->u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwe->u.ap_addr.sa_data, ap->bssid, sizeof(ap->bssid));
        iwe->len = IW_EV_LEN(ap_addr);
        cursor += iwe->len;

        ssid_len = strnlen(ap->ssid, BK7258_WIFI_SCAN_SSID_LEN);
        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWESSID;
        iwe->u.essid.flags = 0;
        iwe->u.essid.length = ssid_len;
        iwe->u.essid.pointer = (FAR void *)sizeof(iwe->u.essid);
        memcpy(&iwe->u.essid + 1, ap->ssid, ssid_len);
        iwe->len = IW_EV_LEN(essid) + ((ssid_len + 3u) & ~3u);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = IWEVQUAL;
        iwe->u.qual.qual = 0;
        iwe->u.qual.level = (uint8_t)ap->rssi;
        iwe->u.qual.noise = 0;
        iwe->u.qual.updated = IW_QUAL_LEVEL_UPDATED | IW_QUAL_DBM |
                              IW_QUAL_QUAL_INVALID | IW_QUAL_NOISE_INVALID;
        iwe->len = IW_EV_LEN(qual);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWFREQ;
        /* WEXT represents values 0..1000 as a channel number. This avoids
         * inventing a frequency conversion for a future/non-2.4GHz result. */
        iwe->u.freq.e = 0;
        iwe->u.freq.m = ap->channel;
        iwe->len = IW_EV_LEN(freq);
        cursor += iwe->len;

        iwe = (FAR struct iw_event *)cursor;
        iwe->cmd = SIOCGIWENCODE;
        iwe->u.data.flags = ap->security == WIFI_SECURITY_NONE ?
                            IW_ENCODE_DISABLED :
                            IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
        iwe->u.data.length = 0;
        iwe->u.data.pointer = NULL;
        iwe->len = IW_EV_LEN(data);
        cursor += iwe->len;
      }

    iwr->u.data.length = cursor - (FAR uint8_t *)iwr->u.data.pointer;
  }

  nxmutex_unlock(&priv->scan_lock);
  return OK;
#else
  (void)lower;
  (void)iwr;
  (void)set;
  return -ENOSYS;
#endif
}

static int bk7258_wifi_range(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr)
{
  return -ENOSYS;
}

/****************************************************************************
 * Ops tables
 ****************************************************************************/

static const struct netdev_ops_s g_bk7258_net_ops =
{
  .ifup     = bk7258_wifi_ifup,
  .ifdown   = bk7258_wifi_ifdown,
  .transmit = bk7258_wifi_transmit,
  .receive  = bk7258_wifi_receive,
  .reclaim  = bk7258_wifi_reclaim,
};

static const struct wireless_ops_s g_bk7258_iw_ops =
{
  .connect    = bk7258_wifi_connect,
  .disconnect = bk7258_wifi_disconnect,
  .essid      = bk7258_wifi_essid,
  .bssid      = bk7258_wifi_bssid,
  .passwd     = bk7258_wifi_passwd,
  .mode       = bk7258_wifi_mode,
  .auth       = bk7258_wifi_auth,
  .country    = bk7258_wifi_country,
  .scan       = bk7258_wifi_scan,
  .range      = bk7258_wifi_range,
};

/****************************************************************************
 * Lifecycle / registration
 ****************************************************************************/

int bk7258_wifi_lower_init(struct bk7258_wifi_s *priv)
{
  memset(priv, 0, sizeof(*priv));

  if (nxmutex_init(&priv->scan_lock) < 0)
    {
      return -ENOMEM;
    }

  priv->scan_lock_ready = true;

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  {
    bk_err_t ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                        bk7258_wifi_scan_done, priv);

    if (ret != BK_OK && ret != BK_ERR_EVENT_CB_EXIST)
      {
        nxmutex_destroy(&priv->scan_lock);
        priv->scan_lock_ready = false;
        return -EIO;
      }

    priv->scan_callback_registered = true;
  }
#endif

  return 0;
}

int bk7258_wifi_lower_uninit(struct bk7258_wifi_s *priv)
{
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  if (priv->scan_callback_registered)
    {
      (void)bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                   bk7258_wifi_scan_done);
      priv->scan_callback_registered = false;
    }
#endif

  if (priv->registered)
    {
      netdev_lower_unregister(PRIV2LOWER(priv));
      priv->registered = false;
    }

  while (priv->rx_head != NULL)
    {
      FAR struct iob_s *iob = bk7258_wifi_receive(PRIV2LOWER(priv));
      if (iob != NULL)
        {
          iob_free_chain(iob);
        }
    }

  if (priv->scan_lock_ready)
    {
      nxmutex_destroy(&priv->scan_lock);
      priv->scan_lock_ready = false;
    }

  return 0;
}

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static bk_err_t bk7258_wifi_scan_done(void *arg, event_module_t module,
                                      int event_id, void *event_data)
{
  FAR struct bk7258_wifi_s *priv = arg;
  FAR wifi_event_scan_done_t *done = event_data;
  wifi_scan_result_t result = {0};
  int ret;
  int ap_num;
  int index;
  struct bk7258_scan_diag_s diag;

  if (priv == NULL || module != EVENT_MOD_WIFI ||
      event_id != EVENT_WIFI_SCAN_DONE || done == NULL ||
      !priv->scan_lock_ready)
    {
      return BK_OK;
    }

  nxmutex_lock(&priv->scan_lock);
  if (!priv->scan_in_progress || done->scan_id != priv->scan_id)
    {
      nxmutex_unlock(&priv->scan_lock);
      return BK_OK;
    }
  nxmutex_unlock(&priv->scan_lock);

  bk7258_scan_diag_record(BK7258_SCAN_DIAG_COMPLETION_CALLBACK);

  /* The SDK allocates result.aps. Copy the bounded data below, then release
   * it unconditionally before this synchronous event callback returns. */

  ret = bk_wifi_scan_get_result(&result);
  if (ret != BK_OK)
    {
      bk7258_scan_diag_record(BK7258_SCAN_DIAG_RESULT_FETCH_FAIL);
    }

  nxmutex_lock(&priv->scan_lock);
  if (priv->scan_in_progress && done->scan_id == priv->scan_id)
    {
      priv->scan_count = 0;
      priv->scan_status = ret == BK_OK ? OK : -EIO;

      if (ret == BK_OK && result.ap_num > 0 && result.aps != NULL)
        {
          ap_num = result.ap_num;
          if (ap_num > BK7258_WIFI_SCAN_MAX_APS)
            {
              ap_num = BK7258_WIFI_SCAN_MAX_APS;
            }

          for (index = 0; index < ap_num; index++)
            {
              FAR struct bk7258_wifi_scan_ap_s *dst = &priv->scan_aps[index];
              FAR wifi_scan_ap_info_t *src = &result.aps[index];

              memset(dst, 0, sizeof(*dst));
              memcpy(dst->ssid, src->ssid, BK7258_WIFI_SCAN_SSID_LEN);
              dst->ssid[BK7258_WIFI_SCAN_SSID_LEN] = '\0';
              memcpy(dst->bssid, src->bssid, sizeof(dst->bssid));
              dst->rssi = src->rssi;
              dst->channel = src->channel;
              dst->security = (uint32_t)src->security;
            }

          priv->scan_count = ap_num;
        }

      priv->scan_in_progress = false;
      priv->scan_complete = true;
    }
  nxmutex_unlock(&priv->scan_lock);

  bk7258_scan_diag_result_exported(ret == BK_OK && result.ap_num > 0 ?
                                  (uint32_t)result.ap_num : 0);
  bk7258_scan_diag_snapshot(&diag);
  {
    /* Hardware observations that discriminate the three scan-probe failure
     * hypotheses in one trace: NX MAC master FSM (0x49100000+0x500/0x504,
     * base as read by libwifi.a scan.c), the channel context pointer that
     * mcc.c chan_is_on_channel() requires to be non-NULL, and the TXL halt
     * flag set inside txl_reset.  No frame content is captured. */
    extern uint8_t chan_env[];
    extern uint8_t txl_cntrl_env[];

    /* Console is 80 columns and overwrites past ~79 chars, so every
     * diagnostic keeps its payload under ~65 chars after the tag.  Three
     * consecutive boards lost fields to this (vcorehsel twice, the whole
     * MMSTART post line once), which is why these are split. */

    syslog(LOG_INFO, "[BK7258-WIFI] d2a fsm=%08lx/%08lx r38=%08lx\n",
           (unsigned long)getreg32(0x49100500),
           (unsigned long)getreg32(0x49100504),
           (unsigned long)getreg32(0x49100038));
    syslog(LOG_INFO, "[BK7258-WIFI] d2b chan=%08lx txhalt=%04x\n",
           (unsigned long)*(volatile uint32_t *)(chan_env + 0x28),
           (unsigned int)*(volatile uint16_t *)(txl_cntrl_env + 0x16e));
    /* Discriminate the crm_mdm_reset path, split into short lines so a
     * 115200 console cannot truncate the fields.  All reads only. */
    syslog(LOG_INFO, "[BK7258-WIFI] d3a ofdm=%lu pwak=%08lx crm14=%08lx\n",
           (unsigned long)bk7258_wifi_pwd_ofdm_get_override(),
           (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP),
           (unsigned long)getreg32(0x49850014));
    /* GEN interrupt triple at scan end: enable/status/ack of the 0x49108000
     * block.  status!=0 means the MAC raised interrupts nobody consumed
     * (host routing problem); status==0 means the MAC never raised one
     * (upstream of the interrupt controller). */
    /* NXMAC free-running counter (0x49100120), read by hal_machw_time().
     * Two spaced samples: a difference of 0 means the machw tick is dead --
     * every timer/timeout in the firmware (chan once-switch, hal_machw_reset
     * park loop) is time-based on this counter. */
    {
      uint32_t t1 = getreg32(0x49100120);
      uint32_t clk1 = getreg32(0x49850008);
      uint32_t t2 = getreg32(0x49100120);

      syslog(LOG_INFO,
             "[BK7258-WIFI] d3b mt=%lu->%lu d=%lu c08=%08lx m38=%08lx\n",
             (unsigned long)t1, (unsigned long)t2,
             (unsigned long)(t2 - t1),
             (unsigned long)clk1,
             (unsigned long)getreg32(0x49100038));
    }
  }
  {
    extern volatile uint32_t bk7258_wifi_isr_count[64];
    extern int ke_state_get(uint8_t task_id);

    uint32_t state = (unsigned)ke_state_get(0);  /* TASK_MM = 0 */

    /* THREE PROBES DELETED HERE (2026-09-01) -- all three dereferenced
     * hard-coded closed-library RAM addresses that have since DRIFTED:
     *
     *   cm      0x280700c0 -> now lands in socket_entity+4 (a 12-byte symbol)
     *   in_doze 0x2807bdb1 -> now lands in scanu_env+733
     *   lowpll  0x2807bdb8 -> now lands in scanu_env+740
     *
     * Proof of drift, measured in THIS session: chan_env / txl_cntrl_env /
     * rwnx_env / ke_env each moved by exactly +0x28 between two builds,
     * because adding scan_ssid[33]+scan_ssid_len to struct bk7258_wifi_s
     * pushed every following BSS symbol along.  Any literal address into
     * library RAM is invalidated by an unrelated struct change.
     *
     * Consequence that must be carried forward: the repeated
     * "in_doze=0 lowpll=0, therefore the MAC is not sleeping" conclusion is
     * WITHDRAWN for our side -- those bytes were never the doze status.  The
     * authority board's own in_doze probe is unaffected (its build, its
     * addresses).
     *
     * Rule: reach library state through a linkable global (extern) or a
     * function call, never through a literal.  chan_env/txl_cntrl_env below
     * are `extern` and therefore resolved at link time; ke_state_get is a
     * call.  Those stay. */

    /* Per-source interrupt counts.  The library registers seven handlers via
     * _bk_int_isr_register (board log: sources 36,35,34,33,31,30,29) and the
     * counters have been collected all along in hw_driver_shim.c -- only
     * index 36 was ever printed.  For the stalled directed scan the decisive
     * one is 34 (MAC TX trigger): the probe frame reaches txl_frame_push yet
     * `frame released without call cb` says its confirm never ran, so either
     * the completion interrupt never arrives (count 0 -> enable/routing, or
     * the MAC never raised it) or it arrives and the dispatch chain drops it
     * (count > 0 -> the HISR/workqueue semantics).
     * Source map (Armino sys_struct.h:463-465 + high-bank bits 0-4):
     *   29 modem_mpb  30 modem_riu  31 mac_txrx_timer
     *   32 txrx_misc  33 rx_trigger 34 tx_trigger  35 prot  36 gen */

    syslog(LOG_INFO, "[BK7258-WIFI] isr 29=%lu 30=%lu 31=%lu 32=%lu\n",
           (unsigned long)bk7258_wifi_isr_count[29],
           (unsigned long)bk7258_wifi_isr_count[30],
           (unsigned long)bk7258_wifi_isr_count[31],
           (unsigned long)bk7258_wifi_isr_count[32]);
    syslog(LOG_INFO, "[BK7258-WIFI] isr 33=%lu 34=%lu 35=%lu 36=%lu\n",
           (unsigned long)bk7258_wifi_isr_count[33],
           (unsigned long)bk7258_wifi_isr_count[34],
           (unsigned long)bk7258_wifi_isr_count[35],
           (unsigned long)bk7258_wifi_isr_count[36]);
    syslog(LOG_INFO, "[BK7258-WIFI] mmstate=%lu\n", (unsigned long)state);
    /* WPROBE3 REMOVED (2026-09-01).  It hard-coded 0x2806fb90 as "the hal env
     * pointer" and dereferenced +0xe4/e8/ec/f8 as setfreq inputs.  nm proves
     * that word holds the closed library's saved FUNCS-TABLE pointer
     * (0x2806f810 = g_wifi_os_funcs), so all four "inputs" resolved +0 to our
     * own slot functions (hp_uap_ip_start, hp_uap_ip_down,
     * hp_net_wlan_add_netif, bk7258_wifi_set_sta_status_cb).  Every reading it
     * produced was garbage.
     *
     * The wider lesson (user directive): hard-referencing closed-library
     * internal RAM from outside is not how this port gets fixed.  Eight such
     * probe-driven candidates were falsified this session, while all six real
     * defects came from diffing OUR sources against Armino's sources.
     * Hardware registers stay fair game -- their semantics are recoverable
     * from Armino's own accessor functions -- but library-internal addresses
     * are not.  Remaining offenders of this kind, kept only because they at
     * least report what they claim: the 0x2807bdb1/0x2807bdb8 doze bytes
     * above and the chan_env/txl_cntrl_env externs in scan diag2. */
    /* Milestone M1 (alignment checklist): 0x49108050 is the interrupt
     * control register hal_machw_init programs last; ==1 means
     * hal_machw_init ran to its interrupt-enable step. */
    syslog(LOG_INFO,
           "[BK7258-WIFI] M1: 49108050=%08x\n",
           (unsigned long)getreg32(0x49108050));
  }
  /* NXMAC register-window dump (0x49100000-0x7F, 32 words = 8 short
   * lines).  Diff against the authoritative board's identical dump to
   * expose every divergent NXMAC register in one pass. */
  for (int wi = 0; wi < 44; wi += 4)
    {
      syslog(LOG_INFO, "[NXWIN] %02x:%08x %08x %08x %08x\n",
             wi * 4,
             (unsigned long)getreg32(0x49100000 + wi * 4),
             (unsigned long)getreg32(0x49100000 + wi * 4 + 4),
             (unsigned long)getreg32(0x49100000 + wi * 4 + 8),
             (unsigned long)getreg32(0x49100000 + wi * 4 + 12));
    }
  /* The `d3f` probe that used to sit here is DELETED (2026-09-01).
   *
   * It sampled 0x49100010 twice across a 200 us delay and printed it as
   * `tick=`, on the belief that the register was a free-running ke_timer /
   * mm_timer counter.  It is not: 0x49100010 / 0x14 are the NXMAC MAC-ADDRESS
   * registers (authority accessors nxmac_mac_addr_low_get / _hi_get read
   * [base+0x10] / [base+0x14]), and the board proves it -- once bk_get_mac was
   * fixed the pair went from 0/0 to 468c47c8 / 00001502, i.e. exactly the
   * c8:47:8c:46:02:15 read out of flash, matching the authority byte for byte.
   * So every earlier "the tick counter is dead" reading was really "the MAC
   * address register is zero", and the 200 us delay measured nothing.
   *
   * NXWIN row 0x10 already prints both words, so nothing is lost by removing
   * this.  0x491000ac was printed alongside it as `ac=` on an equally
   * unverified guess (nxmac_lp_clk_32786_hz_setf) and is dropped with it. */

  /* MM_RESET/MM_START hardware latches, re-reported from this quiet context.
   * Printing at the sample sites raced with other threads' console output: on
   * sta-mmstate-20260901 the `post` line vanished and the `pre` line was cut
   * in half, losing the only measurement that image existed to take.
   *
   * DO NOT read ke=0 / r38=0 at the post-start column as "mm_active() never
   * ran".  A three-way table saying that was written here once and is wrong:
   * mm_start_req_handler sends the CFM *before* it calls mm_active() and
   * ke_state_set(), then parks the MAC via hal_machw_idle_req().  So the
   * instant rw_msg_send() returns is either ahead of mm_active() or already
   * past the park, and ke=0 / r38=0 there is the expected success path.
   * Board evidence (sta-arminoalign-20260902): all four columns read ke=0,
   * r38=0, ret=0, while the end-of-scan probe reports mmstate=MM_ACTIVE(1)
   * and r38=0x33 -- MM did start.  These four columns therefore say nothing
   * about MM bring-up; they are only useful for registers that are expected
   * to be stable across the whole handshake.
   * States: 0=MM_IDLE 1=MM_ACTIVE 2=GOING_TO_IDLE 3=HOST_BYPASSED
   *         4=MM_NO_IDLE (lmac_msg.h:885-899).  ke=-1 marks a site that has
   *         no ke_state to report (the two reset points).
   *
   * This replaces the old mm1/mm2 pair: hp0 carries the same seen/state/ret
   * fields and hp1 the same r38 values, over four sample points instead of
   * two, so keeping both would only spend scarce console lines twice. */

  bk7258_hwprobe_report();
  /* Was a single 294-character line -- the worst truncation offender in the
   * whole diagnostic set; on an 80-column console only its first ~79 chars
   * plus the final character ever arrived, so 12 of these 17 counters were
   * never actually readable.  Four short lines instead. */

  syslog(LOG_INFO, "[BK7258-WIFI] sd1 req=%lu act=%lu pas=%lu\n",
         (unsigned long)diag.requested_channels,
         (unsigned long)diag.active_channels,
         (unsigned long)diag.passive_channels);
  syslog(LOG_INFO, "[BK7258-WIFI] sd2 ind=%lu done=%lu ins=%lu\n",
         (unsigned long)diag.lmac_result_ind,
         (unsigned long)diag.lmac_complete,
         (unsigned long)diag.result_inserted);
  syslog(LOG_INFO, "[BK7258-WIFI] sd3 full=%lu cty=%lu dup=%lu oom=%lu\n",
         (unsigned long)diag.result_table_full,
         (unsigned long)diag.result_country_drop,
         (unsigned long)diag.result_duplicate,
         (unsigned long)diag.result_alloc_fail);
  syslog(LOG_INFO, "[BK7258-WIFI] sd4 bcn=%lu pr=%lu nosta=%lu\n",
         (unsigned long)diag.host_mgmt_beacon,
         (unsigned long)diag.host_mgmt_probe_resp,
         (unsigned long)diag.host_mgmt_no_sta_vif);
  syslog(LOG_INFO, "[BK7258-WIFI] sd5 qdrop=%lu fwd=%lu exp=%lu fail=%lu\n",
         (unsigned long)diag.host_mgmt_wpaq_drop,
         (unsigned long)diag.host_mgmt_wpaq_forwarded,
         (unsigned long)diag.result_exported,
         (unsigned long)diag.result_fetch_fail);

  bk_wifi_scan_free_result(&result);
  return BK_OK;
}
#endif

int bk7258_wifi_lower_register(struct bk7258_wifi_s *priv)
{
  int ret;

  priv->lower.ops = &g_bk7258_net_ops;
#ifdef CONFIG_NETDEV_WIRELESS_HANDLER
  priv->lower.iw_ops = &g_bk7258_iw_ops;
#endif
  atomic_init(&priv->lower.quota[NETPKT_TX], 1);
  atomic_init(&priv->lower.quota[NETPKT_RX], 1);

  ret = netdev_lower_register(PRIV2LOWER(priv), NET_LL_IEEE80211);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  priv->carrier = false;
  return OK;
}

void bk7258_wifi_lower_carrier_on(struct bk7258_wifi_s *priv)
{
  priv->carrier = true;
  netdev_lower_carrier_on(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_carrier_off(struct bk7258_wifi_s *priv)
{
  priv->carrier = false;
  netdev_lower_carrier_off(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_rx_ready(struct bk7258_wifi_s *priv)
{
  netdev_lower_rxready(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_tx_done(struct bk7258_wifi_s *priv)
{
  netdev_lower_txdone(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt)
{
  FAR struct iob_s *iob;
  unsigned int len;

  if (priv == NULL || vpkt == NULL || !priv->registered)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  len = vpkt->tot_len;
  if (len == 0 || len > BK7258_WIFI_FRAME_MAX)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  iob = iob_tryalloc(false);
  if (iob == NULL)
    {
      bk7258_vpkt_free(vpkt);
      return;
    }

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
  if (iob_trycopyin(iob, vpkt->payload, len, 0, false) != (int)len ||
      bk7258_wifi_rx_enqueue(priv, iob) < 0)
    {
      iob_free_chain(iob);
      bk7258_vpkt_free(vpkt);
      return;
    }

  bk7258_vpkt_free(vpkt);
  netdev_lower_rxready(PRIV2LOWER(priv));
}

/****************************************************************************
 * Public entry
 ****************************************************************************/

/* One-line-per-subsystem parity snapshot printed once at init completion.
 * Reads only: no SSID/BSSID/frame data.  The slot list covers every group
 * where the authoritative initializer binds and this port may stay NULL
 * (EVM/ATE, netif/IP glue, power-save stubs, CSI, vendor-IE, airkiss,
 * low-analog, dcache) so a bring-up trace shows the remaining divergence
 * surface without another audit round. */
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
static void bk7258_wifi_parity_banner(void)
{
  static const struct
  {
    const char *name;
    size_t off;
  } slots[] =
  {
    { "_do_evm",                     offsetof(wifi_os_funcs_t, _do_evm) },
    { "_bk_feature_csi_out_cb",      offsetof(wifi_os_funcs_t, _bk_feature_csi_out_cb) },
    { "_send_udp_bc_pkt",            offsetof(wifi_os_funcs_t, _send_udp_bc_pkt) },
    { "_save_net_info",              offsetof(wifi_os_funcs_t, _save_net_info) },
    { "_sta_ip_down",                offsetof(wifi_os_funcs_t, _sta_ip_down) },
    { "_mac_sleeped",                offsetof(wifi_os_funcs_t, _mac_sleeped) },
    { "_mcu_ps_machw_init",          offsetof(wifi_os_funcs_t, _mcu_ps_machw_init) },
    { "_sys_hal_enter_low_analog",   offsetof(wifi_os_funcs_t, _sys_hal_enter_low_analog) },
    { "_flush_all_dcache",           offsetof(wifi_os_funcs_t, _flush_all_dcache) },
    { "_vendor_ie_cb",               offsetof(wifi_os_funcs_t, _bk_wifi_get_vendor_ie_cb_internal) },
  };
  char nulls[144];
  size_t used = 0;

  nulls[0] = '\0';
  for (size_t i = 0; i < nitems(slots); i++)
    {
      if (*(void *const *)((const char *)&g_wifi_os_funcs + slots[i].off) == NULL)
        {
          int written = snprintf(nulls + used, sizeof(nulls) - used,
                                 "%s ", slots[i].name);
          if (written < 0 || (size_t)written >= sizeof(nulls) - used)
            {
              break;
            }
          used += (size_t)written;
        }
    }

  syslog(LOG_INFO,
         "[BK7258-WIFI] parity: null-vs-authoritative: %s\n",
         nulls[0] != '\0' ? nulls : "(none)");
  {
    uint32_t mt1 = getreg32(0x49100120);
    uint32_t mt2 = getreg32(0x49100120);

    /* Split from one 137-char line.  That single line is what made a reader
     * misread `fsm=8`: on an 80-column console the payload was cut right
     * after "fsm=" and the surviving trailing character was crm10's last
     * digit ('8' from 0x00003108), so the init-time FSM value was never
     * actually printed.  Each field now gets a line it fits in. */

    syslog(LOG_INFO, "[BK7258-WIFI] p1 pwak=%08lx clken=%08lx\n",
           (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP),
           (unsigned long)getreg32(BK7258_SYS_DEV_CLK_EN));
    syslog(LOG_INFO, "[BK7258-WIFI] p2 id=%08lx fsm=%08lx r38=%08lx\n",
           (unsigned long)getreg32(0x49100000),
           (unsigned long)getreg32(0x49100504),
           (unsigned long)getreg32(0x49100038));
    syslog(LOG_INFO, "[BK7258-WIFI] p3 crm10=%08lx mt=%lu->%lu\n",
           (unsigned long)getreg32(0x49850010),
           (unsigned long)mt1, (unsigned long)mt2);
  }
}
#endif

bool bk7258_wifi_is_ready(void)
{
  return g_bk7258_wifi.registered;
}

/****************************************************************************
 * STA association
 *
 * Transcribed from the authority's own caller, demo_sta_app_init()
 * (cp/components/bk_wifi/src/wifi_api.c:185-210), which is the variant that
 * takes just an SSID and a passphrase -- no BSSID, no hard-coded channel.
 * That file also sits in our tree byte-identical to the authority copy (it is
 * simply not compiled), so the sequence below can be checked against it line
 * by line.  The two callees are byte-identical to the authority as well: our
 * wifi_v2.c differs from cp/components/bk_wifi/src/wifi_v2.c in only four
 * places (include depth, one #if/#ifdef, the g_wifi_funcs/g_wifi_vars
 * definition that belongs to funcs_fill.c here, and a trailing newline) --
 * none of them inside a function body.
 *
 * Guarded like bk7258_wifi_scan(): bk_wifi_sta_start() reaches
 * wpa_psk_request() and wlan_sta_enable(), whose providers
 * (wpa_psk_cache.c, wpa_ctrl_iface.c, sa_station.c) are inside the
 * CONFIG_BK7258_WIFI_VENDOR_RUNTIME block of CMakeLists.txt, so the
 * runtime-disabled CP image must not reference them.
 ****************************************************************************/

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME

/* Both vendor buffers are NUL-terminated (wifi_types.h:63 and :67):
 * WIFI_SSID_STR_LEN == 32+1, WIFI_PASSWORD_LEN == 64+1.  A WPA2 passphrase is
 * 8..63 characters, or exactly 64 hex digits when a raw PMK is supplied. */

#define BK7258_WIFI_PSK_MIN_LEN  8u

int bk7258_wifi_sta_connect(FAR const char *ssid, FAR const char *psk)
{
  wifi_sta_config_t config;
  size_t ssid_len;
  size_t psk_len;
  bk_err_t ret;

  if (ssid == NULL || psk == NULL)
    {
      return -EINVAL;
    }

  /* The vendor call chain asserts on an uninitialized stack: bk_wifi_init()
   * must have run so that cfg_param_init() has allocated g_sta_param_ptr,
   * which bk_wifi_sta_set_config() dereferences without a NULL check. */

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  ssid_len = strlen(ssid);
  psk_len = strlen(psk);

  /* DELIBERATE DEVIATION from the transcribed original, which uses
   * os_strcpy() for both fields and length-checks only the SSID
   * (wifi_api.c:191-204).  That is safe there because its arguments are
   * compile-time constants; ours arrive from a command line, so an
   * over-long argument would run off a 33- or 65-byte struct member.  The
   * copies below are bounded and both lengths are rejected up front. */

  if (ssid_len == 0u || ssid_len >= sizeof(config.ssid))
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: ssid length %u, need 1..%u\n",
             (unsigned)ssid_len, (unsigned)(sizeof(config.ssid) - 1u));
      return -EINVAL;
    }

  if (psk_len < BK7258_WIFI_PSK_MIN_LEN || psk_len >= sizeof(config.password))
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: psk length %u, need %u..%u\n",
             (unsigned)psk_len, (unsigned)BK7258_WIFI_PSK_MIN_LEN,
             (unsigned)(sizeof(config.password) - 1u));
      return -EINVAL;
    }

  /* Zeroed exactly as the original does with `= {0}`.  Two consequences are
   * load-bearing rather than incidental:
   *   - reserved[32] must be all zero or wifi_sta_validate_config() rejects
   *     the config outright (wifi_v2.c:2669, WIFI_RESERVED_BYTE_VALUE == 0);
   *   - is_user_fast_connect must stay 0, otherwise validate_config takes the
   *     g_fci overwrite branch at wifi_v2.c:2672.
   * security is left 0 (== WIFI_SECURITY_NONE) because the original leaves it
   * so; the STA path never reads it -- wifi_sta_set_global_config()
   * (wifi_v2.c:2732) copies ssid and password but not security.
   */

  memset(&config, 0, sizeof(config));
  memcpy(config.ssid, ssid, ssid_len);
  memcpy(config.password, psk, psk_len);

  /* SSID and lengths only.  The passphrase itself is never logged, here or
   * in the status path. */

  syslog(LOG_INFO, "[BK7258-WIFI] connect: ssid=\"%s\" (%u), psk %u chars\n",
         config.ssid, (unsigned)ssid_len, (unsigned)psk_len);

  /* set_config before start, as in the original.  set_config is also what
   * populates g_sta_param_ptr->ssid/key, which is what wpa_psk_request()
   * reads inside bk_wifi_sta_start() (wifi_v2.c:2477); calling start alone
   * would derive a PSK from an empty SSID and key.  If a link is already up,
   * set_config disconnects first and re-connects on its own
   * (wifi_v2.c:2983-3010). */

  ret = bk_wifi_sta_set_config(&config);
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: set_config failed=%d\n",
             (int)ret);
      return -EIO;
    }

  ret = bk_wifi_sta_start();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] connect: sta_start failed=%d\n",
             (int)ret);
      return -EIO;
    }

  /* Association and the 4-way handshake run asynchronously from here; the
   * caller polls bk7258_wifi_sta_connect_status(). */

  syslog(LOG_INFO, "[BK7258-WIFI] connect: request accepted\n");
  return OK;
}

int bk7258_wifi_sta_connect_status(FAR int *state, FAR int *reason)
{
  wifi_linkstate_reason_t info;
  bk_err_t ret;

  if (!bk7258_wifi_is_ready())
    {
      return -ENODEV;
    }

  /* bk_wifi_sta_get_linkstate_with_reason(), not bk_wifi_sta_get_link_status():
   * the latter returns early with a bare DISCONNECTED once
   * wifi_sta_is_connected() is false (wifi_v2.c:3078-3082), which discards the
   * reason code precisely when a failure needs explaining.  This one reads
   * mhdr_get_station_status() directly and keeps both fields. */

  memset(&info, 0, sizeof(info));
  ret = bk_wifi_sta_get_linkstate_with_reason(&info);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (state != NULL)
    {
      *state = (int)info.state;
    }

  if (reason != NULL)
    {
      *reason = (int)info.reason_code;
    }

  return OK;
}

bool bk7258_wifi_sta_is_connected(void)
{
  int state;

  /* Same test as the vendor's own wifi_netif_sta_is_connected()
   * (cp/components/bk_wifi/src/wifi_netif.c:157-160): compare the link state
   * against CONNECTED exactly.  Note the comparison is strict, so a link that
   * has advanced to GOT_IP would read as not-connected -- the vendor keeps a
   * separate wifi_netif_sta_is_got_ip() for that.  That cannot happen here:
   * CONFIG_LWIP is unset in this profile, so nothing runs a DHCP client and
   * CONNECTED is the terminal state of the association path we implement. */

  if (bk7258_wifi_sta_connect_status(&state, NULL) < 0)
    {
      return false;
    }

  return state == WIFI_LINKSTATE_STA_CONNECTED;
}

FAR const char *bk7258_wifi_sta_state_str(int state)
{
  switch (state)
    {
      case WIFI_LINKSTATE_STA_IDLE:           return "IDLE";
      case WIFI_LINKSTATE_STA_CONNECTING:     return "CONNECTING";
      case WIFI_LINKSTATE_STA_DISCONNECTED:   return "DISCONNECTED";
      case WIFI_LINKSTATE_STA_CONNECTED:      return "CONNECTED";
      case WIFI_LINKSTATE_STA_CONNECT_FAILED: return "CONNECT_FAILED";
      case WIFI_LINKSTATE_STA_GOT_IP:         return "GOT_IP";
      case WIFI_LINKSTATE_STA_SCAN_DONE:      return "SCAN_DONE";
      default:                                return "unknown";
    }
}

FAR const char *bk7258_wifi_sta_reason_str(int reason)
{
  /* Only the codes that plausibly end a WPA2-PSK association attempt are
   * named; the caller prints the raw number too, so an unnamed code is still
   * traceable to wifi_types.h.  WIFI_REASON_MAX is the vendor's "connected
   * successfully" sentinel, not an error (wifi_types.h:246). */

  switch (reason)
    {
      case WIFI_REASON_MAX:
        return "SUCCESS";
      case WIFI_REASON_RESERVED:
        return "none";
      case WIFI_REASON_UNSPECIFIED:
        return "UNSPECIFIED";
      case WIFI_REASON_PREV_AUTH_NOT_VALID:
        return "PREV_AUTH_NOT_VALID";
      case WIFI_REASON_DEAUTH_LEAVING:
        return "DEAUTH_LEAVING";
      case WIFI_REASON_MICHAEL_MIC_FAILURE:
        return "MICHAEL_MIC_FAILURE";
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_HANDSHAKE_TIMEOUT";
      case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        return "GROUP_KEY_UPDATE_TIMEOUT";
      case WIFI_REASON_IE_IN_4WAY_DIFFERS:
        return "IE_IN_4WAY_DIFFERS";
      case WIFI_REASON_GROUP_CIPHER_NOT_VALID:
        return "GROUP_CIPHER_NOT_VALID";
      case WIFI_REASON_PAIRWISE_CIPHER_NOT_VALID:
        return "PAIRWISE_CIPHER_NOT_VALID";
      case WIFI_REASON_AKMP_NOT_VALID:
        return "AKMP_NOT_VALID";
      case WIFI_REASON_IEEE_802_1X_AUTH_FAILED:
        return "IEEE_802_1X_AUTH_FAILED";
      case WIFI_REASON_CIPHER_SUITE_REJECTED:
        return "CIPHER_SUITE_REJECTED";
      case WIFI_REASON_BEACON_LOST:
        return "BEACON_LOST";
      case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
      case WIFI_REASON_WRONG_PASSWORD:
        return "WRONG_PASSWORD";
      case WIFI_REASON_DISCONNECT_BY_APP:
        return "DISCONNECT_BY_APP";
      case WIFI_REASON_DHCP_TIMEOUT:
        return "DHCP_TIMEOUT";
      default:
        return "unknown";
    }
}

#else /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

int bk7258_wifi_sta_connect(FAR const char *ssid, FAR const char *psk)
{
  return -ENOSYS;
}

int bk7258_wifi_sta_connect_status(FAR int *state, FAR int *reason)
{
  return -ENOSYS;
}

bool bk7258_wifi_sta_is_connected(void)
{
  return false;
}

FAR const char *bk7258_wifi_sta_state_str(int state)
{
  return "unsupported";
}

FAR const char *bk7258_wifi_sta_reason_str(int reason)
{
  return "unsupported";
}

#endif /* CONFIG_BK7258_WIFI_VENDOR_RUNTIME */

int bk7258_wifi_initialize(void)
{
  int ret;

  /* The scan command auto-initializes; a later explicit init must not run
   * the power/clock/vendor sequence twice. */
  if (g_bk7258_wifi.registered)
    {
      return OK;
    }

  /* bk_init.c ordering alignment: vote CPU to 120M and apply the vendor
   * calibration overlay BEFORE any wifi/phy/calibration path runs --
   * delay10us/200us loops and the calibration sequencer assume the CPU
   * is at the voted frequency, and the calibration tables must be in
   * place before calibration_init consumes them inside bk_wifi_init. */
  {
    extern bk_err_t bk_pm_module_vote_cpu_freq(uint32_t dev, uint32_t frq);
    extern void hp_dvfs_log_state(const char *tag);
    bk_err_t freq_ret;

    /* pm.h:340 PM_DEV_ID_DEFAULT == 41 and pm.h:349 PM_CPU_FRQ_120M == 3.
     * The previous (26, 2) passed PM_DEV_ID_MAC / PM_CPU_FRQ_80M -- harmless
     * only while the callee ignored its arguments. */
    hp_dvfs_log_state("pre-vote");
    freq_ret = bk_pm_module_vote_cpu_freq(41 /* PM_DEV_ID_DEFAULT */,
                                         3 /* PM_CPU_FRQ_120M */);
    hp_dvfs_log_state("post-vote");
    syslog(LOG_INFO,
           "[BK7258-WIFI] boot: cpu_freq vote 120M ret=%d\n", (int)freq_ret);
  }
  ret = bk7258_wifi_osal_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_hw_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_board_init();
  if (ret < 0)
    {
      return ret;
    }

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  ret = bk_event_init();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: event init failed=%d\n", ret);
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }

  /* LPO source alignment, MOVED HERE 2026-09-01 (was after
   * bk7258_wifi_lower_register, i.e. after bk_wifi_init had already run).
   *
   * The authoritative build carries CONFIG_DEFAULT_LPO_SRC=2 (= ROSC) as a
   * BUILD-TIME setting, so on that board R41.lpo_config is already ROSC
   * before any vendor library executes.  Our port programmed it late, which
   * means libbk_phy.a (bk_phy/rf_adapter_init below) and libwifi.a
   * (bk_wifi_init) both observed the reset default first and the final value
   * only afterwards.  That matters because the MM_START_REQ handler is
   * documented to call _bk_pm_lpo_src_get() twice and compare the answers:
   * a source that changes underneath the library is a divergence we control.
   *
   * Placed before bk_phy_adapter_init so BOTH archives see one stable value
   * for their whole lifetime. */
  {
    /* AON PMU R41.lpo_config (bits[1:0]): 0=DIVD 1=X32K 2=ROSC. */
    uint32_t r41 = getreg32(BK7258_AON_PMU_R41);

    r41 = (r41 & ~BK7258_AON_PMU_R41_LPO_CONFIG_MASK) | UINT32_C(2);
    putreg32(r41, BK7258_AON_PMU_R41);
    syslog(LOG_INFO,
           "[BK7258-WIFI] pmq: lpo_src set to ROSC (pre-adapter) r41=0x%08lx\n",
           (unsigned long)getreg32(BK7258_AON_PMU_R41));
  }

  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bind adapters\n");
  bk_phy_adapter_init();
  ret = bk_rf_adapter_init();
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: RF adapter validation failed=%d\n",
             ret);
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }

  /* vnd_cal overlay: Armino runs it after app_phy_init/bk_rf_adapter_init
   * and before app_wifi_init (phy/SARADC/analog access path ready, tables
   * in place before calibration_init consumes them inside bk_wifi_init).
   * Calling it before this point crashed inside the closed-source
   * vnd_cal_set_epa_config (float logging path with uninitialized
   * infrastructure). */
  {
    extern void vnd_cal_overlay(void);

    vnd_cal_overlay();
    syslog(LOG_INFO, "[BK7258-WIFI] vnd_cal overlay applied\n");
  }
  /* power_clk_rf_init is deliberately NOT called here.
   *
   * The authoritative bk7258 build never executes it: driver.c:271 guards
   * the call with CONFIG_POWER_CLOCK_RF, Kconfig defaults it to n, and the
   * reference iperf build carries no such define (sdkconfig.cmake:429 sets
   * it empty).  ROSC calibration, temp-detect enable and the R41 bit24
   * rosc->wifi route therefore never run on the authoritative board, so
   * running them here manufactured a divergence rather than closing one.
   * See investigation/bk7258-bk-only/init-sequence-comparison.md.
   */

  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bk_wifi_init begin\n");
  ret = bk_wifi_init(&(wifi_init_config_t)WIFI_DEFAULT_INIT_CONFIG());
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] runtime: bk_wifi_init failed=%d\n",
             ret);
      bk_event_deinit();
      bk7258_wifi_hw_deinit();
      return -ENODEV;
    }
  syslog(LOG_INFO, "[BK7258-WIFI] runtime: bk_wifi_init complete\n");
#endif

  ret = bk7258_wifi_lower_init(&g_bk7258_wifi);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_lower_register(&g_bk7258_wifi);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258-WIFI] lower register failed=%d\n", ret);
    }
#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
  /* Clock-root fix (see investigation external-analysis-and-clock-root-cause
   * .md §3): rwnx_env+0xa8 (the 80 MHz mode code feeding
   * rwnxl_covert_cpu_freq -> rwnxl_compute_cpu_freq -> crm_clk_set) is never
   * initialized by the pinned archive, so crm_clk_set ran with clk_config
   * row 0, leaving 0x49000000=0 and the NXMAC core without a functional
   * clock.  Both functions are global exports of libwifi.a; seeding the
   * vote slot and applying row 1 restores the authoritative clock state. */
  /* The LPO-source write that used to sit here has MOVED to just before
   * bk_phy_adapter_init (see the comment there).  Programming it at this
   * point was too late to be an alignment: bk_wifi_init had already run, so
   * libbk_phy.a and libwifi.a observed the reset default for their whole
   * init and the intended value only afterwards -- and the old comment's
   * claim "before any libwifi clock/scan path runs" was simply false. */
  /* Diagnostic experiment writes REMOVED (2026-09-01).  Three writes lived
   * here -- 0x49100054=0x10000, 0x49100010=0xDEAD0000 and
   * 0x49850010=0x108 -- and they poisoned every register table quoted as
   * parity evidence: any NXWIN/RSTWIN reading of those addresses reflected
   * our own write rather than hardware state.  The 0x49100054 write also
   * mis-attributed the bit5 divergence to ourselves, when in fact it
   * CLEARS bit5 and the closed library sets it during scan.  No
   * authoritative code writes these addresses from the host side; keep the
   * init path free of experiment writes so register evidence stays valid.
   *
   * Fix v2 seed ROLLED BACK (2026-09-01): the authoritative-board WPROBE
   * comparison proved the archive leaves rwnx_env+0xa8 at its native 0
   * through MM_START and the CRM stays on clk_config row 0
   * (0x49000000=0 / 0x49850008=0x108 / 0x49850010=0x108) while the NXMAC
   * FSM is fully active (0x49100504=0x40000000).  Seeding a8=1 and calling
   * crm_clk_set(1) here diverged from the authoritative behavior (it also
   * wrote 0x49850010=0x3108, bits[13:12] force) and is not needed: row 0
   * IS the working configuration on both platforms. */
  bk7258_wifi_parity_banner();
#endif

  /* CPU frequency alignment, tail half (2026-09-02).
   *
   * The authority votes the CPU TWICE, and the order matters:
   *   bk_init.c:267  vote(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_120M)
   *   bk_init.c:292  app_wifi_init() -> bk_netif_init() + bk_wifi_init()
   *   bk_init.c:372  #if CONFIG_CPU_DEFAULT_FREQ_60M
   *   bk_init.c:373      vote(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_60M)
   *
   * So the authority also brings Wi-Fi up at 120M and only downshifts to 60M
   * afterwards; the working reference log's `clkdiv=0x00100037` (ckdiv_core=7,
   * 60M) is the state AFTER init, not during it.  The vote at the head of this
   * function therefore stays at 120M -- replacing it with 60M would run
   * calibration and bk_wifi_init at a frequency the authority never uses.
   *
   * CONFIG_CPU_DEFAULT_FREQ_60M is 1 in the authority build
   * (build/bk7258/iperf/bk7258/config/sdkconfig.h:231), and PM_CPU_FRQ_60M is
   * 1 (pm.h:347), reusing PM_DEV_ID_DEFAULT == 41 from the 120M vote above so
   * this replaces that module's vote rather than adding a second voter. */

  {
    extern bk_err_t bk_pm_module_vote_cpu_freq(uint32_t dev, uint32_t frq);
    extern void hp_dvfs_log_state(const char *tag);
    bk_err_t freq60_ret;

    freq60_ret = bk_pm_module_vote_cpu_freq(41 /* PM_DEV_ID_DEFAULT */,
                                           1 /* PM_CPU_FRQ_60M */);
    hp_dvfs_log_state("post-init-60m");
    syslog(LOG_INFO,
           "[BK7258-WIFI] init done: cpu_freq vote 60M ret=%d\n",
           (int)freq60_ret);
  }

  return ret;
}
