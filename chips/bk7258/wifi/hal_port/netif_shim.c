#include <nuttx/config.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "components/netif.h"

/* arp_send() is in NuttX's internal ARP header.  Declare it directly so
 * this shim can send the Armino ARP keepalive without pulling in the
 * full internal net stack headers. */
extern int arp_send(in_addr_t ipaddr);

/* Declares sta_ip_mode_set() below with the vendor's exact signature, so this
 * shim cannot drift from it.  The header is the port-local copy and pulls in
 * only stdint/stdbool. */

#include "lwip_intf_v2_1/lwip-2.1.2/port/net.h"

bk_err_t bk_netif_init(void) { return BK_OK; }
bk_err_t bk_netif_get_ip4_config(netif_if_t ifx, netif_ip4_config_t *config)
{
  struct net_driver_s *dev;
  const char *name = ifx == NETIF_IF_STA ? "wlan0" : "wlan1";
  if (config == NULL) return BK_ERR_NULL_PARAM;
  dev = netdev_findbyname(name);
  if (dev == NULL) return BK_ERR_NO_DEV;
  if (dev->d_ipaddr == 0) {
    config->ip[0] = config->mask[0] = config->gateway[0] = config->dns[0] = '\0';
  } else {
    inet_ntop(AF_INET, &dev->d_ipaddr, config->ip, NETIF_IP4_STR_LEN);
    inet_ntop(AF_INET, &dev->d_netmask, config->mask, NETIF_IP4_STR_LEN);
    inet_ntop(AF_INET, &dev->d_draddr, config->gateway, NETIF_IP4_STR_LEN);
    config->dns[0] = '\0';
  }
  return BK_OK;
}
bk_err_t bk_netif_set_ip4_config(netif_if_t ifx, const netif_ip4_config_t *config)
{ (void)ifx; (void)config; return BK_ERR_NOT_SUPPORT; }
bk_err_t bk_netif_get_ip6_addr_info(netif_if_t ifx)
{ (void)ifx; return BK_FAIL; }
bk_err_t bk_netif_dhcpc_start(netif_if_t ifx)
{ (void)ifx; return BK_ERR_NOT_SUPPORT; }
bk_err_t bk_netif_static_ip(netif_ip4_config_t config)
{ (void)config; return BK_ERR_NOT_SUPPORT; }

void net_begin_send_arp_reply(int is_send_arp, int is_allow_send_req)
{
  /* Armino's ARP keepalive: the MAC receives a beacon, the closed lib
   * sends MM_INDICATE_SEND_ARP_IND, and the host emits an ARP frame so
   * the AP sees the STA is alive.  Without this, the AP times out the
   * STA after ~60 s of silence.
   *
   * Armino's lwIP version calls net_begin_send_arp_reply to trigger an
   * ARP reply/request.  NuttX owns ARP (CONFIG_LWIP=0), so we send the
   * keepalive through NuttX's arp_send() -- the same contract, different
   * stack.  is_send_arp=true means "send ARP now"; is_allow_send_req
   * gates whether ARP requests are permitted (we always allow). */
  if (is_send_arp)
    {
      FAR struct net_driver_s *dev = netdev_findbyname("wlan0");
      if (dev != NULL && dev->d_draddr != 0)
        {
          (void)arp_send(dev->d_draddr);
        }
    }

  (void)is_allow_send_req;
}

void sta_ip_mode_set(int dhcp)
{
  /* Same file and same reasoning as net_begin_send_arp_reply() above: the
   * vendor original lives in the lwIP port (net.c:973) and selects an lwIP
   * addressing mode -- ip_address_set(1, DHCP_CLIENT, ...) for dhcp==1, a
   * flash-stored static address for dhcp==2, a static address for dhcp==0.
   * This profile has no lwIP (CONFIG_LWIP is unset), and NuttX owns the
   * interface address and the DHCP lease, so there is no vendor-side mode to
   * select and nothing to forward this to.
   *
   * It exists purely for linkage.  All five call sites are inside
   * wifi_v2.c -- :2152 and :2418 plus the three at :2404/:2406/:2409 -- and
   * every one of them sits behind bk_feature_fast_dhcp_enable() or
   * bk_feature_fast_connect_enable(), both of which return 0 in this build
   * (hal_port/feature_shim.c:38-45 and :47-58).  So this is unreachable at
   * runtime, but wifi_v2.c is compiled as one translation unit and the STA
   * association path keeps its objects alive, so the symbol must still
   * resolve: "never executed" is not "never linked".
   *
   * Deliberately silent rather than logging: if the fast-DHCP or fast-connect
   * features are ever turned on, the call becomes reachable and a warning here
   * would fire on a normal path.  The place to notice that is this comment and
   * the feature shim, not a runtime message.
   */

  (void)dhcp;
}
