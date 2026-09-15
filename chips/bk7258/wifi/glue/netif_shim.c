#include <nuttx/config.h>
#include <nuttx/net/netdev.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "components/netif.h"

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
  /* The vendor indication asks lwIP to emit an active ARP reply. NuttX owns
   * ARP and this S1/S2 port has no validated carrier/TX/IP lifecycle yet, so
   * the optimization is deliberately disabled rather than synthesizing a
   * packet behind the network stack's back. */
  (void)is_send_arp;
  (void)is_allow_send_req;
}
