/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_netdev.c
 *
 * The network side the kept Armino Wi-Fi sources call, on NuttX
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). The baseline provides it
 * in lwip_intf_v2_1/lwip-2.1.2/port (net.c, wlanif.c), which drives lwip's
 * stack; lwip's protocol code is excluded, so the same entry points drive a
 * NuttX netdev_lowerhalf "wlan0" instead. Only lwip's pbuf is kept, as the
 * Wi-Fi buffer.
 *
 * Mapping (baseline -> here):
 *  - wlanif.c ethernetif_input(): EAPOL to wpa_supplicant, everything else
 *    copied into a netpkt and handed to the NuttX stack
 *  - wlanif.c low_level_output(): netpkt copied into a PBUF_RAW_TX pbuf for
 *    bmsg_tx_sender(), on the vif of the registered STA netif
 *  - net.c sta_ip_start()/sta_ip_down(): bk_netif.c calls them on
 *    EVENT_WIFI_STA_CONNECTED/DISCONNECTED, so they also own the carrier.
 *    DHCP is NuttX's dhcpc, and on a lease the same two notifications as
 *    wm_netif_status_callback() (net.c:405-406) reach Armino's state machine
 *  - net.c ip_address_set()/sta_ip_mode_set(): same sta_ip_settings record
 *
 * Not carried, each logged when reached: SoftAP addressing and its DHCP
 * server (uap_ip_*, dhcp_lookup_mac), the keep-alive ARP replies
 * (net_begin_send_arp_reply), and the lwip ping/etharp CLI helpers.
 *
 * Compiled with NuttX's flags; the Armino and lwip types are restated from
 * lwip_netif_address.h, lwip/pbuf.h (leading fields only), bk_patch/
 * sk_intf.h and netif_types.h. The dhcpc/netlib prototypes are restated
 * from apps/include/netutils, which is not on the arch include path.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/semaphore.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/net/netdev_lowerhalf.h>

#define ARMINO_IFNAME          "wlan0"
#define ARMINO_ETH_HDR_LEN     14u
#define ARMINO_ETH_SRC_OFFSET  6u
#define ARMINO_ETH_TYPE_OFFSET 12u
#define ARMINO_ETHTYPE_EAPOL   0x888eu
#define ARMINO_ETHTYPE_ARP     0x0806u
#define ARMINO_ARP_FRAME_LEN   42u     /* Ethernet 14 + ARP 28 */
#define ARMINO_ARP_REQUEST     1
#define ARMINO_ARP_REPLY       2
#define ARMINO_FRAME_MAX       1514u

#define ARMINO_DHCPC_PRIORITY  100
#define ARMINO_DHCPC_STACKSIZE 4096

/* lwip/pbuf.h: PBUF_RAW_TX is PBUF_LINK_ENCAPSULATION_HLEN, which lwipopts.h
 * sets to CONFIG_MSDU_RESV_HEAD_LENGTH + CONFIG_MSDU_RESV_DESC_LENGTH
 * (108 + 600 in the baseline sdkconfig); PBUF_RAM is
 * PBUF_ALLOC_FLAG_DATA_CONTIGUOUS | PBUF_TYPE_FLAG_STRUCT_DATA_CONTIGUOUS.
 */

#define ARMINO_PBUF_RAW_TX     (108 + 600)
#define ARMINO_PBUF_RAM        0x0280

/* lwip_netif_address.h */

#define ADDR_TYPE_DHCP         0
#define ADDR_TYPE_STATIC       1
#define ADDR_TYPE_FAST_DHCP    2

/* bk_wifi.h enum ip_ver; netif_types.h netif_if_t. */

#define ARMINO_IP4             (1 << 4)
#define ARMINO_NETIF_IF_STA    0

/* ponytail: the leading fields of lwip's struct pbuf only; every field this
 * file touches sits before the first optional member (pbuf.h:199-212).
 */

struct pbuf
{
  struct pbuf *next;
  void *payload;
  uint16_t tot_len;
  uint16_t len;
};

struct ipv4_config
{
  unsigned addr_type;
  unsigned address;
  unsigned gw;
  unsigned netmask;
  unsigned dns1;
  unsigned dns2;
};

/* CONFIG_IPV6 is unset in the baseline, so this is the whole struct. */

struct wlan_ip_config
{
  struct ipv4_config ipv4;
};

struct ke_sk_params
{
  unsigned char *buf;
  int len;
  int flag;
  uint32_t freq;
};

struct dhcpc_state
{
  struct in_addr serverid;
  struct in_addr ipaddr;
  struct in_addr netmask;
  struct in_addr dnsaddr;
  struct in_addr default_router;
  uint32_t       lease_time;
};

/* The opaque handles net_get_sta_handle()/net_get_uap_handle() return.
 * ponytail: kept CLI code (bk_netif.c:157) casts them to lwip's struct
 * netif and reads its address fields; the zeroed pad reads as "no
 * address". Size it from lwip's struct netif if such a reader ever needs
 * more.
 */

struct armino_iface_s
{
  uint8_t pad[256];
};

/* Armino and lwip. */

struct pbuf *pbuf_alloc(int layer, uint16_t length, int type);
uint8_t pbuf_free(struct pbuf *p);
int bmsg_tx_sender(struct pbuf *p, uint32_t vif_idx);
int bmsg_special_tx_sender(struct pbuf *p, uint32_t vif_idx);
int ke_l2_packet_tx(const struct ke_sk_params *params);
void *wifi_netif_mac_to_vif(uint8_t *mac);
int wifi_netif_vif_to_vifid(void *vif);
int wifi_netif_vif_to_netif_type(void *vif);
bool wifi_netif_sta_is_connected(void);
void wifi_netif_notify_sta_got_ip(int ver);
void wifi_netif_call_status_cb_when_sta_got_ip(void);
void wifi_netif_call_status_cb_when_sta_dhcp_timeout(void);

/* apps/netutils. */

void *dhcpc_open(const char *interface, const void *mac_addr, int mac_len);
int dhcpc_request(void *handle, struct dhcpc_state *presult);
void dhcpc_cancel(void *handle);
void dhcpc_close(void *handle);
int netlib_ifup(const char *ifname);
int netlib_ifdown(const char *ifname);
int netlib_set_ipv4addr(const char *ifname, const struct in_addr *addr);
int netlib_set_dripv4addr(const char *ifname, const struct in_addr *addr);
int netlib_set_ipv4netmask(const char *ifname, const struct in_addr *addr);
int netlib_set_ipv4dnsaddr(const struct in_addr *inaddr);

/* The baseline's port/net.h interface, defined here. */

void net_wlan_initial(void);
int net_wlan_add_netif(uint8_t *mac);
int net_wlan_remove_netif(uint8_t *mac);
void *net_get_sta_handle(void);
void *net_get_uap_handle(void);
int net_get_if_addr(struct wlan_ip_config *addr, void *intrfc_handle);
void sta_ip_start(void);
void sta_ip_down(void);
uint32_t sta_ip_is_start(void);
void sta_ip_mode_set(int dhcp);
void uap_ip_start(void);
void uap_ip_down(void);
uint32_t uap_ip_is_start(void);
void ip_address_set(int iface, int dhcp, char *ip, char *mask, char *gw,
                    char *dns);
void net_begin_send_arp_reply(bool is_send_arp, bool is_allow_send_req);
void ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx);
void ping_start(char *target_name, uint32_t times, size_t size);
void ping_stop(void);
int8_t etharp_request(void *netif, const void *ipaddr);
uint8_t *dhcp_lookup_mac(uint8_t *chaddr);

struct armino_netdev_s
{
  struct netdev_lowerhalf_s lower;   /* Must be first */
  FAR struct iob_s *rx_head;
  FAR struct iob_s *rx_tail;
  bool registered;
  bool sta_added;                    /* net_wlan_add_netif() for the STA */
  uint8_t vif_idx;
  uint8_t mac[6];
};

static struct armino_netdev_s g_netdev;
static struct armino_iface_s g_sta_iface;
static struct armino_iface_s g_uap_iface;

/* sta_ip_settings/uap_ip_settings and the start flags of net.c. g_lock
 * also covers the DHCP session: sta_ip_down() may run while it waits.
 */

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static struct ipv4_config g_sta_settings;
static struct ipv4_config g_uap_settings;
static struct ipv4_config g_sta_addr;   /* Address in use, 0 if none */
static bool g_sta_ip_started;
static bool g_uap_ip_started;
static void *g_dhcpc;
static unsigned int g_sta_generation;  /* Bumped by every sta_ip_down() */
static sem_t g_dhcp_wake = SEM_INITIALIZER(0);  /* Ends a worker's sleep */

/* The gateway's MAC, from the last ARP frame it sent us, for the keep-alive
 * reply. Both written on the Wi-Fi core thread, MAC before IP.
 * ponytail: a reader on another thread may pair a new IP with a MAC being
 * rewritten; it costs one wrong keep-alive frame after a gateway change.
 */

static uint8_t g_gw_mac[6];
static volatile uint32_t g_gw_mac_ip;

/****************************************************************************
 * Data plane
 ****************************************************************************/

static int armino_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  return OK;
}

static int armino_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  return OK;
}

/* low_level_output() (wlanif.c:230-262). bmsg_tx_sender() takes its own
 * reference and drops it if the queue push fails, so the pbuf_alloc()
 * reference is released here on every path, as a7de5d2 ran it on the board.
 */

static int armino_transmit(FAR struct netdev_lowerhalf_s *dev,
                           FAR netpkt_t *pkt)
{
  FAR struct armino_netdev_s *priv = (FAR struct armino_netdev_s *)dev;
  unsigned int len = netpkt_getdatalen(dev, pkt);
  struct pbuf *p;
  int ret;

  if (!priv->sta_added)
    {
      return -ENETDOWN;
    }

  if (len == 0 || len > ARMINO_FRAME_MAX)
    {
      return -EMSGSIZE;
    }

  p = pbuf_alloc(ARMINO_PBUF_RAW_TX, (uint16_t)len, ARMINO_PBUF_RAM);
  if (p == NULL)
    {
      return -ENOMEM;
    }

  ret = netpkt_copyout(dev, p->payload, pkt, len, 0);
  if (ret < 0)
    {
      pbuf_free(p);
      return ret;
    }

  ret = bmsg_tx_sender(p, priv->vif_idx);
  pbuf_free(p);
  if (ret != 0)
    {
      return -EIO;
    }

  netpkt_free(dev, pkt, NETPKT_TX);
  netdev_lower_txdone(dev);
  return OK;
}

static FAR netpkt_t *armino_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct armino_netdev_s *priv = (FAR struct armino_netdev_s *)dev;
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

/* TX netpkts are freed in transmit() itself, so there is nothing to reclaim;
 * kept so netdev_upper_can_tx() has its recovery path.
 */

static void armino_reclaim(FAR struct netdev_lowerhalf_s *dev)
{
}

static const struct netdev_ops_s g_netdev_ops =
{
  .ifup     = armino_ifup,
  .ifdown   = armino_ifdown,
  .transmit = armino_transmit,
  .receive  = armino_receive,
  .reclaim  = armino_reclaim,
};

/* ethernetif_input() (wlanif.c:331-451), called by rwnx_rx.c on the Wi-Fi
 * core thread. EAPOL goes to wpa_supplicant first, even without a
 * registered netif, and our own looped-back frames are dropped, both as in
 * the baseline. netpkt_copyin() at offset 0 puts the Ethernet header in the
 * link-layer reserve, where the NuttX stack reads it.
 */

void ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx)
{
  FAR struct armino_netdev_s *priv = &g_netdev;
  FAR const uint8_t *eth;
  FAR struct iob_s *iob;
  struct pbuf *q;
  irqstate_t flags;
  int offset = 0;

  (void)dst_idx;

  if (p == NULL)
    {
      return;
    }

  eth = p->payload;
  if (p->len <= ARMINO_ETH_HDR_LEN || p->tot_len > ARMINO_FRAME_MAX)
    {
      pbuf_free(p);
      return;
    }

  if ((((uint16_t)eth[ARMINO_ETH_TYPE_OFFSET] << 8) |
       eth[ARMINO_ETH_TYPE_OFFSET + 1]) == ARMINO_ETHTYPE_EAPOL)
    {
      if (!priv->sta_added ||
          memcmp(priv->mac, eth + ARMINO_ETH_SRC_OFFSET, 6) != 0)
        {
          struct ke_sk_params params =
            {
              .buf  = p->payload,
              .len  = p->len,
              .flag = iface,
              .freq = 0,
            };

          ke_l2_packet_tx(&params);
        }

      pbuf_free(p);
      return;
    }

  if (!priv->registered || !priv->sta_added)
    {
      pbuf_free(p);
      return;
    }

  /* Learn the gateway MAC for net_begin_send_arp_reply(): sender MAC at 22,
   * sender IP at 28 (network order, as g_sta_addr).
   */

  if (p->len >= ARMINO_ARP_FRAME_LEN &&
      (((uint16_t)eth[ARMINO_ETH_TYPE_OFFSET] << 8) |
       eth[ARMINO_ETH_TYPE_OFFSET + 1]) == ARMINO_ETHTYPE_ARP)
    {
      uint32_t spa;

      memcpy(&spa, eth + 28, sizeof(spa));
      if (spa != 0 && spa == g_sta_addr.gw)
        {
          memcpy(g_gw_mac, eth + 22, sizeof(g_gw_mac));
          g_gw_mac_ip = spa;
        }
    }

  iob = netpkt_alloc(&priv->lower, NETPKT_RX);
  if (iob == NULL)
    {
      pbuf_free(p);
      return;
    }

  for (q = p; q != NULL; q = q->next)
    {
      if (netpkt_copyin(&priv->lower, iob, q->payload, q->len, offset) < 0)
        {
          netpkt_free(&priv->lower, iob, NETPKT_RX);
          pbuf_free(p);
          return;
        }

      offset += q->len;
    }

  pbuf_free(p);

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

  netdev_lower_rxready(&priv->lower);
}

/****************************************************************************
 * Interface lifecycle (net.c)
 ****************************************************************************/

/* net_wlan_initial() (net.c:1254) brings up lwip's stack at boot, from
 * bk_netif_init(): there tcpip_init() reaches lwip_init() (net.c:227-239,
 * init.c:346-352). The NuttX stack is already up, so only the buffer side
 * of that is done here -- stats_init(), mem_init(), memp_init(), in
 * lwip_init()'s order -- plus registering the netdev, which makes wlan0
 * visible before any association. sys_init() is not needed: it only
 * creates the baseline sys_arch's mutexes, and
 * compat/bk7258_lwip_sys.c has its own. pbuf_init() is an empty macro.
 *
 * Without mem_init() mem_mutex stays NULL and the first pbuf_alloc()
 * asserts in sys_mutex_lock; without memp_init() every memp_malloc()
 * returns NULL and RX frames are dropped. Board boot 2026-10-03 hit the
 * first of those at scan result upload.
 */

/* lwip_init()'s buffer-side modules (init.c:346-352), from the kept
 * lwip units. Not declared in lwip/opt.h-visible headers we can include
 * from this NuttX-compiled file; bodies live in mem.c, memp.c, stats.c.
 */

void stats_init(void);
void mem_init(void);
void memp_init(void);

void net_wlan_initial(void)
{
  int ret;

  if (g_netdev.registered)
    {
      return;
    }

  stats_init();
  mem_init();
  memp_init();

  g_netdev.lower.ops = &g_netdev_ops;
  atomic_set(&g_netdev.lower.quota[NETPKT_TX], 1);

  /* RX quota 4, the value a7de5d2 found on the board: with 1, an ARP reply
   * arriving next to broadcast traffic was dropped.
   */

  atomic_set(&g_netdev.lower.quota[NETPKT_RX], 4);

  ret = netdev_lower_register(&g_netdev.lower, NET_LL_IEEE80211);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[ARMINO] netdev register failed: %d\n", ret);
      return;
    }

  g_netdev.registered = true;
}

/* net_wlan_add_netif() (net.c:1271), called when a vif is created. Only the
 * STA vif has a netdev; the vif index and the MAC are the ones
 * low_level_init() and low_level_output() read from the vif.
 */

int net_wlan_add_netif(uint8_t *mac)
{
  void *vif = wifi_netif_mac_to_vif(mac);
  int vifid;

  if (vif == NULL ||
      wifi_netif_vif_to_netif_type(vif) != ARMINO_NETIF_IF_STA)
    {
      syslog(LOG_WARNING, "[ARMINO] add netif: only STA is supported\n");
      return -1;
    }

  vifid = wifi_netif_vif_to_vifid(vif);
  if (vifid < 0 || vifid == 0xff)
    {
      return -1;
    }

  g_netdev.vif_idx = (uint8_t)vifid;
  memcpy(g_netdev.mac, mac, sizeof(g_netdev.mac));
  memcpy(g_netdev.lower.netdev.d_mac.ether.ether_addr_octet, mac,
         sizeof(g_netdev.mac));
  g_netdev.sta_added = true;
  return 0;
}

int net_wlan_remove_netif(uint8_t *mac)
{
  if (!g_netdev.sta_added || memcmp(g_netdev.mac, mac, 6) != 0)
    {
      return -1;
    }

  g_netdev.sta_added = false;
  return 0;
}

void *net_get_sta_handle(void)
{
  return &g_sta_iface;
}

void *net_get_uap_handle(void)
{
  return &g_uap_iface;
}

int net_get_if_addr(struct wlan_ip_config *addr, void *intrfc_handle)
{
  if (intrfc_handle == &g_sta_iface)
    {
      nxmutex_lock(&g_lock);
      addr->ipv4 = g_sta_addr;
      nxmutex_unlock(&g_lock);
    }

  return 0;
}

/****************************************************************************
 * STA addressing (net.c)
 ****************************************************************************/

static void armino_set_addr(const struct ipv4_config *a)
{
  struct in_addr in;

  in.s_addr = a->address;
  netlib_set_ipv4addr(ARMINO_IFNAME, &in);
  in.s_addr = a->netmask;
  netlib_set_ipv4netmask(ARMINO_IFNAME, &in);
  in.s_addr = a->gw;
  netlib_set_dripv4addr(ARMINO_IFNAME, &in);
  if (a->dns1 != 0)
    {
      in.s_addr = a->dns1;
      netlib_set_ipv4dnsaddr(&in);
    }
}

/* The baseline's address-acquired path: wm_netif_status_callback() for
 * DHCP (net.c:405-406), wm_netif_status_static_callback() for a static
 * address (net.c:304), which sends only the notification.
 */

static void armino_sta_got_ip(const struct ipv4_config *a, bool dhcp)
{
  syslog(LOG_INFO, "[ARMINO] wlan0 ip %s\n",
         inet_ntoa((struct in_addr){ .s_addr = a->address }));

  if (dhcp)
    {
      wifi_netif_call_status_cb_when_sta_got_ip();
    }

  wifi_netif_notify_sta_got_ip(ARMINO_IP4);
}

/* Sleep until start + span ticks, or until the session ends. Returns false
 * if sta_ip_down() ended it. A wake meant for another session's worker
 * only costs a recheck.
 */

static bool armino_dhcp_sleep(unsigned int generation, clock_t start,
                              clock_t span)
{
  for (; ; )
    {
      clock_t elapsed = clock_systime_ticks() - start;
      bool live;

      nxmutex_lock(&g_lock);
      live = generation == g_sta_generation;
      nxmutex_unlock(&g_lock);

      if (!live)
        {
          return false;
        }

      if (elapsed >= span)
        {
          return true;
        }

      nxsem_tickwait(&g_dhcp_wake, span - elapsed);
    }
}

/* One dhcpc exchange. On success the full address is applied, on a NAK it
 * is removed, as lwip does (dhcp_handle_nak()). -ESHUTDOWN: the session
 * ended during the exchange.
 */

static int armino_dhcp_request(void *handle, unsigned int generation,
                               uint32_t *lease)
{
  static const struct ipv4_config zero;
  struct dhcpc_state ds;
  struct ipv4_config a;
  int ret;

  /* dhcpc_request() sets errno only for a NAK (ECONNREFUSED) and a cancel
   * (EINTR); a timeout returns ERROR with errno untouched.
   */

  memset(&ds, 0, sizeof(ds));
  errno = 0;
  ret = dhcpc_request(handle, &ds);
  if (ret < 0)
    {
      ret = errno == ECONNREFUSED ? -ECONNREFUSED : -ETIMEDOUT;
    }

  nxmutex_lock(&g_lock);
  if (generation != g_sta_generation)
    {
      /* dhcpc_cancel() is only polled between retries, so an ACK that
       * arrived after sta_ip_down() was already written to wlan0 by dhcpc
       * itself (dhcpc.c:845). Undo it, unless a new session holds an
       * address by now.
       */

      if (g_sta_addr.address == 0)
        {
          armino_set_addr(&zero);
        }

      nxmutex_unlock(&g_lock);
      return -ESHUTDOWN;
    }

  if (ret < 0)
    {
      if (ret == -ECONNREFUSED)
        {
          armino_set_addr(&zero);
          g_sta_addr = zero;
        }

      nxmutex_unlock(&g_lock);
      return ret;
    }

  memset(&a, 0, sizeof(a));
  a.addr_type = ADDR_TYPE_DHCP;
  a.address = ds.ipaddr.s_addr;
  a.netmask = ds.netmask.s_addr;
  a.gw = ds.default_router.s_addr;
  a.dns1 = ds.dnsaddr.s_addr;
  armino_set_addr(&a);
  g_sta_addr = a;
  nxmutex_unlock(&g_lock);

  *lease = ds.lease_time;
  return OK;
}

/* The STA DHCP client for one connection, on the timeline of the baseline's
 * lwip DHCP (dhcp.c), with NuttX's dhcpc doing each exchange:
 *
 *  - no lease yet: retry with lwip's discovery backoff, 2^tries seconds up
 *    to 60 (dhcp_discover(); lwip also has a 250 ms base as a build option)
 *  - T1 = lease/2 renew, T2 = lease*7/8 rebind (dhcp_handle_ack() defaults)
 *  - a failed renew/rebind retries halfway to the next deadline while that
 *    is at least 60 s away (dhcp_t1_timeout(), dhcp_t2_timeout())
 *  - T0 = lease: the address is dropped and discovery restarts
 *    (dhcp_coarse_tmr())
 *  - lease times are capped at 0xffff * 20 s, the range of lwip's 20 s
 *    coarse-timer counters, and are at least one 20 s tick, as there
 *    (dhcp_bind()); 0xffffffff is an infinite lease
 *
 * got_ip goes to Armino once per connection, as net.c's ip4_addr_set flag
 * does (net.c:400). The DHCP-timeout status callback goes out when a held
 * address is lost (NAK or T0): that is when lwip's address change reaches
 * net.c's status callback with DHCP not bound (net.c:466-471). A failing
 * discovery is retried silently, as lwip does.
 *
 * Deviations from lwip, both from dhcpc: every exchange starts with a
 * broadcast DISCOVER, where lwip renews with a unicast REQUEST to the
 * server; and an expired lease is dropped without a DHCPRELEASE.
 */

#define ARMINO_DHCP_TICK       20u   /* DHCP_COARSE_TIMER_SECS */
#define ARMINO_DHCP_LEASE_MAX  (0xffffu * ARMINO_DHCP_TICK)
#define ARMINO_DHCP_RETRY_MIN  60u

static int armino_dhcpc_main(int argc, char *argv[])
{
  static const struct ipv4_config zero;
  unsigned int generation = strtoul(argv[1], NULL, 10);
  bool notified = false;
  bool failing = false;
  unsigned int tries = 0;
  void *handle;
  uint32_t lease;
  int ret;

  /* On a quick reconnect the previous session's worker may still hold UDP
   * port 68 until its exchange polls the cancel (up to the dhcpc retries),
   * so opening is retried while this session lasts.
   */

  while ((handle = dhcpc_open(ARMINO_IFNAME, g_netdev.mac,
                              sizeof(g_netdev.mac))) == NULL)
    {
      if (!failing)
        {
          failing = true;
          syslog(LOG_WARNING, "[ARMINO] dhcpc_open failed, retrying\n");
        }

      if (!armino_dhcp_sleep(generation, clock_systime_ticks(),
                             SEC2TICK(1)))
        {
          return 0;
        }
    }

  failing = false;

  nxmutex_lock(&g_lock);
  ret = generation == g_sta_generation ? OK : -ESHUTDOWN;
  if (ret == OK)
    {
      g_dhcpc = handle;
    }

  nxmutex_unlock(&g_lock);

  while (ret != -ESHUTDOWN)
    {
      uint32_t t0;
      uint32_t t1;
      uint32_t t2;
      uint32_t next;
      clock_t bound;

      /* No lease: discover. */

      ret = armino_dhcp_request(handle, generation, &lease);
      if (ret == -ESHUTDOWN)
        {
          break;
        }

      if (ret < 0)
        {
          if (!failing)
            {
              failing = true;
              syslog(LOG_WARNING, "[ARMINO] DHCP failed: %d, retrying\n",
                     ret);
            }

          tries = tries < 255 ? tries + 1 : tries;
          if (!armino_dhcp_sleep(generation, clock_systime_ticks(),
                                 SEC2TICK(tries < 6 ? 1u << tries : 60u)))
            {
              break;
            }

          continue;
        }

      failing = false;
      tries = 0;
      if (!notified)
        {
          notified = true;
          armino_sta_got_ip(&g_sta_addr, true);
        }

      /* Bound: renew and rebind until the lease is lost. */

      for (; ; )
        {
          bound = clock_systime_ticks();
          if (lease == 0xffffffffu)
            {
              while (armino_dhcp_sleep(generation, clock_systime_ticks(),
                                       SEC2TICK(3600)));
              ret = -ESHUTDOWN;
              break;
            }

          t0 = lease < ARMINO_DHCP_LEASE_MAX ? lease : ARMINO_DHCP_LEASE_MAX;
          t0 = t0 > ARMINO_DHCP_TICK ? t0 : ARMINO_DHCP_TICK;
          t1 = t0 / 2;
          t2 = (t0 / 8) * 7;
          next = t1;

          for (; ; )
            {
              uint32_t elapsed;
              uint32_t half;

              if (!armino_dhcp_sleep(generation, bound, SEC2TICK(next)))
                {
                  ret = -ESHUTDOWN;
                  break;
                }

              elapsed = TICK2SEC(clock_systime_ticks() - bound);
              if (elapsed >= t0)
                {
                  syslog(LOG_WARNING, "[ARMINO] DHCP lease expired\n");
                  nxmutex_lock(&g_lock);
                  armino_set_addr(&zero);
                  g_sta_addr = zero;
                  nxmutex_unlock(&g_lock);
                  ret = -ETIMEDOUT;
                  break;
                }

              ret = armino_dhcp_request(handle, generation, &lease);
              if (ret == OK || ret == -ESHUTDOWN || ret == -ECONNREFUSED)
                {
                  break;
                }

              /* Renew or rebind failed; the address stays until T0. */

              if (elapsed < t2)
                {
                  half = (t2 - elapsed) / 2;
                  next = half >= ARMINO_DHCP_RETRY_MIN ? elapsed + half : t2;
                }
              else
                {
                  half = (t0 - elapsed) / 2;
                  next = half >= ARMINO_DHCP_RETRY_MIN ? elapsed + half : t0;
                }
            }

          if (ret != OK)
            {
              /* -ECONNREFUSED is a NAK, -ETIMEDOUT here only T0: both
               * removed the address.
               */

              if (ret != -ESHUTDOWN)
                {
                  wifi_netif_call_status_cb_when_sta_dhcp_timeout();
                }

              break;
            }
        }
    }

  nxmutex_lock(&g_lock);
  if (g_dhcpc == handle)
    {
      g_dhcpc = NULL;
    }

  nxmutex_unlock(&g_lock);
  dhcpc_close(handle);
  return 0;
}

/* sta_ip_start() (net.c:651-667): the first call after a connect configures
 * the address; a repeated call only re-sends got_ip if one is held.
 */

void sta_ip_start(void)
{
  struct ipv4_config a;
  char generation[12];
  FAR char *argv[2] =
    {
      generation, NULL
    };

  int ret;

  nxmutex_lock(&g_lock);
  if (g_sta_ip_started)
    {
      a = g_sta_addr;
      nxmutex_unlock(&g_lock);
      if (wifi_netif_sta_is_connected() && a.address != 0)
        {
          wifi_netif_notify_sta_got_ip(ARMINO_IP4);
        }

      return;
    }

  g_sta_ip_started = true;
  a = g_sta_settings;
  snprintf(generation, sizeof(generation), "%u", g_sta_generation);
  nxmutex_unlock(&g_lock);

  syslog(LOG_INFO, "[ARMINO] sta ip start (%s)\n",
         a.addr_type == ADDR_TYPE_STATIC ? "static" : "dhcp");

  netdev_lower_carrier_on(&g_netdev.lower);
  netlib_ifup(ARMINO_IFNAME);

  if (a.addr_type == ADDR_TYPE_STATIC)
    {
      nxmutex_lock(&g_lock);
      armino_set_addr(&a);
      g_sta_addr = a;
      nxmutex_unlock(&g_lock);
      armino_sta_got_ip(&a, false);
      return;
    }

  /* ADDR_TYPE_FAST_DHCP reuses an address kept in flash by the baseline;
   * nothing stores one here (sta_ip_mode_set() below), so it is DHCP.
   */

  ret = kthread_create("armino-dhcpc", ARMINO_DHCPC_PRIORITY,
                       ARMINO_DHCPC_STACKSIZE, armino_dhcpc_main, argv);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[ARMINO] dhcpc thread failed: %d\n", ret);
      wifi_netif_call_status_cb_when_sta_dhcp_timeout();
    }
}

void sta_ip_down(void)
{
  static const struct ipv4_config zero;

  nxmutex_lock(&g_lock);
  if (!g_sta_ip_started)
    {
      nxmutex_unlock(&g_lock);
      return;
    }

  syslog(LOG_INFO, "[ARMINO] sta ip down\n");
  g_sta_ip_started = false;
  g_sta_generation++;
  if (g_dhcpc != NULL)
    {
      dhcpc_cancel(g_dhcpc);
    }

  nxsem_post(&g_dhcp_wake);

  armino_set_addr(&zero);
  g_sta_addr = zero;
  nxmutex_unlock(&g_lock);

  netlib_ifdown(ARMINO_IFNAME);
  netdev_lower_carrier_off(&g_netdev.lower);
}

uint32_t sta_ip_is_start(void)
{
  return g_sta_ip_started;
}

/* ip_address_set() (net.c:906-937): iface 0 is the SoftAP, 1 the STA. */

void ip_address_set(int iface, int dhcp, char *ip, char *mask, char *gw,
                    char *dns)
{
  struct ipv4_config addr;

  memset(&addr, 0, sizeof(addr));
  if (dhcp == 1)
    {
      addr.addr_type = ADDR_TYPE_DHCP;
    }
  else
    {
      addr.addr_type = ADDR_TYPE_STATIC;
      addr.address = inet_addr(ip);
      addr.netmask = inet_addr(mask);
      if (addr.netmask == 0xffffffff)
        {
          addr.netmask = 0x00ffffff;   /* 255.255.255.0, as the baseline */
        }

      addr.gw = inet_addr(gw);
      addr.dns1 = inet_addr(dns);
    }

  nxmutex_lock(&g_lock);
  if (iface == 0)
    {
      g_uap_settings = addr;
    }
  else if (iface == 1)
    {
      g_sta_settings = addr;
    }

  nxmutex_unlock(&g_lock);
}

/* sta_ip_mode_set() (net.c:973-1006): 1 is DHCP; 2 is fast DHCP, which
 * falls back to DHCP in the baseline too when flash holds no address, and
 * flash never does here; 0 keeps the current address as a static one.
 */

void sta_ip_mode_set(int dhcp)
{
  nxmutex_lock(&g_lock);
  if (dhcp == 1 || dhcp == 2)
    {
      memset(&g_sta_settings, 0, sizeof(g_sta_settings));
      g_sta_settings.addr_type = ADDR_TYPE_DHCP;
    }
  else
    {
      g_sta_settings = g_sta_addr;
      g_sta_settings.addr_type = ADDR_TYPE_STATIC;
    }

  nxmutex_unlock(&g_lock);
}

/****************************************************************************
 * Not carried in phase one
 ****************************************************************************/

void uap_ip_start(void)
{
  syslog(LOG_WARNING, "[ARMINO] SoftAP addressing unsupported\n");
  g_uap_ip_started = true;
}

void uap_ip_down(void)
{
  g_uap_ip_started = false;
}

uint32_t uap_ip_is_start(void)
{
  return g_uap_ip_started;
}

/* No SoftAP DHCP server: no client has a lease. Both callers handle NULL. */

uint8_t *dhcp_lookup_mac(uint8_t *chaddr)
{
  (void)chaddr;
  return NULL;
}

/* Build one ARP frame and send it the way lwip's special_arp_flag does:
 * through bmsg_special_tx_sender() rather than the data queue
 * (etharp.c:287-320 sets the flag, wlanif.c low_level_output() reads it).
 * Same reference rule as armino_transmit(): the sender takes its own.
 */

static void armino_arp_send(uint16_t op, const uint8_t *ethdst,
                            const uint8_t *hwdst, uint32_t ipsrc,
                            uint32_t ipdst)
{
  struct pbuf *p;
  uint8_t *f;

  p = pbuf_alloc(ARMINO_PBUF_RAW_TX, ARMINO_ARP_FRAME_LEN, ARMINO_PBUF_RAM);
  if (p == NULL)
    {
      return;
    }

  /* Ethernet header, then the ARP body as etharp_raw() fills it. */

  f = p->payload;
  memcpy(f, ethdst, 6);
  memcpy(f + 6, g_netdev.mac, 6);
  f[12] = 0x08;
  f[13] = 0x06;
  f[14] = 0x00;                         /* hwtype Ethernet */
  f[15] = 0x01;
  f[16] = 0x08;                         /* proto IPv4 */
  f[17] = 0x00;
  f[18] = 6;
  f[19] = 4;
  f[20] = (uint8_t)(op >> 8);
  f[21] = (uint8_t)op;
  memcpy(f + 22, g_netdev.mac, 6);
  memcpy(f + 28, &ipsrc, 4);
  memcpy(f + 32, hwdst, 6);
  memcpy(f + 38, &ipdst, 4);

  bmsg_special_tx_sender(p, g_netdev.vif_idx);
  pbuf_free(p);
}

/* net_begin_send_arp_reply() (net.c:1382-1401) and etharp_reply()
 * (etharp.c:290-320). The firmware asks for it with ME_INDICATE_SEND_ARP_IND
 * (rw_msg_rx.c:1736, CONFIG_WIFI6): send the gateway an unsolicited ARP
 * reply with our address, or an ARP request for it if its MAC is unknown.
 *
 * Deviations, both from NuttX:
 *  - the gateway MAC is the one last seen in an ARP frame from it (see
 *    ethernetif_input()), not a lookup in the stack's ARP table:
 *    arp_find() is internal to net/, and the public SIOCGARP takes
 *    net_lock(), which this Wi-Fi core thread must not wait on while the
 *    stack may be waiting on this thread's TX queue
 *  - etharp_tmr_flag (ARP entries kept 3600 instead of arp_timeout coarse
 *    ticks while set, etharp.c:243) has no NuttX counterpart; NuttX ages
 *    entries by CONFIG_NET_ARP_MAXAGE. ponytail: add a permanent entry for
 *    the gateway with netlib_set_arpmapping() if that ever matters.
 */

void net_begin_send_arp_reply(bool is_send_arp, bool is_allow_send_req)
{
  uint32_t ip;
  uint32_t gw;

  /* The baseline's two flag cases; any other combination sends. */

  if (!is_send_arp && is_allow_send_req)
    {
      return;
    }

  if (!g_netdev.sta_added)
    {
      return;
    }

  /* Read without g_lock, which the DHCP worker holds across netlib calls
   * that take net_lock(); see above. Word reads; a lease change between
   * the two only affects this one frame.
   */

  ip = g_sta_addr.address;
  gw = g_sta_addr.gw;
  if (gw == 0)
    {
      return;
    }

  if (g_gw_mac_ip == gw)
    {
      armino_arp_send(ARMINO_ARP_REPLY, g_gw_mac, g_gw_mac, ip, gw);
    }
  else
    {
      static const uint8_t bcast[6] =
        {
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        };

      static const uint8_t zero[6];

      armino_arp_send(ARMINO_ARP_REQUEST, bcast, zero, ip, gw);
    }
}

/* lwip CLI helpers of cli_netif.c and wifi_at.c; NSH's ping replaces them. */

void ping_start(char *target_name, uint32_t times, size_t size)
{
  syslog(LOG_WARNING, "[ARMINO] lwip ping unsupported, use NSH ping\n");
}

void ping_stop(void)
{
}

/* Used only by iperf.c:405 to prime the lwip ARP cache. */

int8_t etharp_request(void *netif, const void *ipaddr)
{
  return 0;
}
