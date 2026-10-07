/****************************************************************************
 * chips/bk7258/wifi_armino/lwip_os/lwip/netif.h
 *
 * Replaces lwip-2.1.2/src/include/lwip/netif.h for phase one
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7), in the same seam as
 * sockets.h/inet.h/netdb.h/stats.h.
 *
 * Almost all of the original is fine and kept: struct netif, the
 * netif_find/NETIF_FOREACH family, and the lwip-internal address helpers
 * that kept code uses.  One declaration is not: netif.h:559 has
 *
 *     u8_t if_nametoindex(const char *name);
 *
 * while nuttx's net/if.h:389 has
 *
 *     unsigned int if_nametoindex(FAR const char *ifname);
 *
 * and the two are incompatible.  The clash was invisible as long as no
 * translation unit saw both headers.  Turning on
 * CONFIG_NETDEV_WIRELESS_IOCTL makes <sys/ioctl.h> pull
 * nuttx/wireless/wireless.h, which pulls <net/if.h>, so any Armino unit
 * that reaches <sys/ioctl.h> through lwip_os/lwip/sockets.h now sees both
 * -- cli_lwip.c was the first.
 *
 * The socket API is NuttX's (S2.7), so if_nametoindex is NuttX's too and
 * the lwip spelling is the one to drop.  It is renamed out of the way for
 * the duration of the include; no kept unit calls that spelling, and the
 * real one comes from NuttX's libc.
 ****************************************************************************/

#ifndef LWIP_HDR_NETIF_H_REPLACEMENT
#define LWIP_HDR_NETIF_H_REPLACEMENT

#define if_nametoindex lwip_unused_if_nametoindex
#include "../../../armino_baseline/cp/components/lwip_intf_v2_1/lwip-2.1.2/src/include/lwip/netif.h"
#undef if_nametoindex

#endif /* LWIP_HDR_NETIF_H_REPLACEMENT */
