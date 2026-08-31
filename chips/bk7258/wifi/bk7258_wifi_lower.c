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

#if CONFIG_BK7258_WIFI_VENDOR_RUNTIME
#include <components/event.h>
#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include "bk_phy_adapter.h"
#include "bk_rf_adapter.h"
#include <syslog.h>
#endif

extern int bmsg_tx_sender(struct pbuf *p, uint32_t vif_idx);

/* os/os.h defines this as beken_thread_t *; beken_thread_t is void *. Keep
 * the declaration local so this NuttX lower-half does not import its broad
 * compatibility macro surface merely to obtain the SDK scan request token. */

extern void **rtos_get_current_thread(void);
extern uint32_t sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm(void);

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
 * copied into the NuttX queue and released only after the copy completes. */
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
  return -ENOSYS;
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
      ret = bk_wifi_scan_start(NULL);
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

    syslog(LOG_INFO,
           "[BK7258-WIFI] scan diag2: macfsm=0x%08lx/0x%08lx "
           "chan_ctx=0x%08lx txhalt=0x%04x start38=0x%08lx\n",
           (unsigned long)getreg32(0x49100500),
           (unsigned long)getreg32(0x49100504),
           (unsigned long)*(volatile uint32_t *)(chan_env + 0x28),
           (unsigned int)*(volatile uint16_t *)(txl_cntrl_env + 0x16e),
           (unsigned long)getreg32(0x49100038));
    /* Discriminate the crm_mdm_reset path, split into short lines so a
     * 115200 console cannot truncate the fields.  All reads only. */
    syslog(LOG_INFO,
           "[BK7258-WIFI] diag3a: pwd_ofdm=%lu pwakeup=0x%08lx crm14=0x%08lx\n",
           (unsigned long)sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm(),
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
             "[BK7258-WIFI] diag3b: machwtime=%lu->%lu (delta=%lu) "
             "crm08=0x%08lx mac38=0x%08lx\n",
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
    uint32_t cm = *(volatile uint32_t *)0x280700c0u;

    syslog(LOG_INFO,
           "[BK7258-WIFI] diag3c: isr36=%lu mmstate=%lu cm=0x%08lx\n",
           (unsigned long)bk7258_wifi_isr_count[36], state, cm);
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
  {
    /* mm_check_clk_change() is the exportable entry that programs the
     * NXMAC LP-clock (nxmac_lp_clk_32786_hz_setf -> +0xAC) after a host
     * clock change.  The authoritative board's internal timer counter
     * (0x49100010) free-runs; ours read 0, freezing ke_timer expiry and
     * the whole chan mechanism.  Call it once at init tail, then sample
     * the counter twice to prove it ticks. */
    extern void mm_check_clk_change(unsigned int type);
    uint32_t t1 = getreg32(0x49100010);

    mm_check_clk_change(0);

    uint32_t t2 = getreg32(0x49100010);
    uint32_t ac = getreg32(0x491000ac);

    syslog(LOG_INFO,
           "[BK7258-WIFI] diag3f: clktick=0x%08lx->0x%08lx lpac=0x%08lx\n",
           (unsigned long)t1, (unsigned long)t2, (unsigned long)ac);
  }
  syslog(LOG_INFO,
         "[BK7258-WIFI] scan diag: req=%lu active=%lu passive=%lu "
         "lmac_ind=%lu lmac_done=%lu insert=%lu full=%lu country=%lu "
         "dup=%lu oom=%lu host_bcn=%lu host_pr=%lu host_nosta=%lu "
         "host_qdrop=%lu host_fwd=%lu exported=%lu fetch_fail=%lu\n",
         (unsigned long)diag.requested_channels,
         (unsigned long)diag.active_channels,
         (unsigned long)diag.passive_channels,
         (unsigned long)diag.lmac_result_ind,
         (unsigned long)diag.lmac_complete,
         (unsigned long)diag.result_inserted,
         (unsigned long)diag.result_table_full,
         (unsigned long)diag.result_country_drop,
         (unsigned long)diag.result_duplicate,
         (unsigned long)diag.result_alloc_fail,
         (unsigned long)diag.host_mgmt_beacon,
         (unsigned long)diag.host_mgmt_probe_resp,
         (unsigned long)diag.host_mgmt_no_sta_vif,
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

    syslog(LOG_INFO,
           "[BK7258-WIFI] parity: pwakeup=0x%08lx clken=0x%08lx "
           "macid=0x%08lx fsm=0x%08lx start38=0x%08lx crm10=0x%08lx\n",
           (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP),
           (unsigned long)getreg32(BK7258_SYS_DEV_CLK_EN),
           (unsigned long)getreg32(0x49100000),
           (unsigned long)getreg32(0x49100504),
           (unsigned long)getreg32(0x49100038),
           (unsigned long)getreg32(0x49850010));
    syslog(LOG_INFO,
           "[BK7258-WIFI] parity: machwtime=%lu->%lu (delta=%lu)\n",
           (unsigned long)mt1, (unsigned long)mt2,
           (unsigned long)(mt2 - mt1));
  }
}
#endif

bool bk7258_wifi_is_ready(void)
{
  return g_bk7258_wifi.registered;
}

int bk7258_wifi_initialize(void)
{
  int ret;

  /* The scan command auto-initializes; a later explicit init must not run
   * the power/clock/vendor sequence twice. */
  if (g_bk7258_wifi.registered)
    {
      return OK;
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
  /* LPO source alignment (2026-09-01): the authoritative board's AON PMU
   * R41=0x232 carries lpo_config=PM_LPO_SRC_ROSC; our boot chain leaves
   * the reset default 0 (DIVD), so the library receives a different LPO
   * source answer.  Program ROSC here to match the authoritative
   * environment before any libwifi clock/scan path runs. */
  {
    /* AON PMU R41.lpo_config (bits[1:0]): 0=DIVD 1=X32K 2=ROSC.
     * Authoritative board runs ROSC. */
    uint32_t r41 = getreg32(BK7258_AON_PMU_R41);

    r41 = (r41 & ~BK7258_AON_PMU_R41_LPO_CONFIG_MASK) | UINT32_C(2);
    putreg32(r41, BK7258_AON_PMU_R41);
    syslog(LOG_INFO,
           "[BK7258-WIFI] pmq: lpo_src set to ROSC\n");
  }
  /* Behavioral alignment experiments (2026-09-01):
   * 1) 0x49100054: our builds carry bits[5]+[12] that the authoritative
   *    board never sets (it reads 0x10000).  Force the authoritative
   *    value at init; the scan-end NXWIN shows whether something
   *    re-sets them (persistent doze-path writer) or the alignment
   *    sticks (one-shot init-time write).
   * 2) 0x49100010: the internal timer counter reads 0 while the
   *    authoritative board counts (0x468c47c8).  Write a marker; the
   *    scan-end NXWIN discriminates: marker intact = counter dead,
   *    changed value = counter runs (was merely never started). */
  putreg32(UINT32_C(0x10000), 0x49100054);
  putreg32(UINT32_C(0xDEAD0000), 0x49100010);
  /* Experiment 3: force CRM macclk force bits (0x3000) off -- the
   * authoritative board keeps 0x49850010=0x108 while ours reads 0x3108.
   * If the 40MHz MAC domain (0x10 counter / 0x504 FSM) comes alive
   * after this, the 0x3000 force bits were gating the domain. */
  putreg32(UINT32_C(0x108), 0x49850010);
  /* Fix v2 seed ROLLED BACK (2026-09-01): the authoritative-board WPROBE
   * comparison proved the archive leaves rwnx_env+0xa8 at its native 0
   * through MM_START and the CRM stays on clk_config row 0
   * (0x49000000=0 / 0x49850008=0x108 / 0x49850010=0x108) while the NXMAC
   * FSM is fully active (0x49100504=0x40000000).  Seeding a8=1 and calling
   * crm_clk_set(1) here diverged from the authoritative behavior (it also
   * wrote 0x49850010=0x3108, bits[13:12] force) and is not needed: row 0
   * IS the working configuration on both platforms. */
  bk7258_wifi_parity_banner();
#endif
  return ret;
}
