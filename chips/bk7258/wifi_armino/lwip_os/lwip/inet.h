/****************************************************************************
 * chips/bk7258/wifi_armino/lwip_os/lwip/inet.h
 *
 * Replaces lwip-2.1.2/src/include/lwip/inet.h for phase one
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). in_addr_t, struct
 * in_addr, struct in6_addr and inet_addr()/inet_aton() are OS interfaces
 * and come from NuttX; the lwip copy redefined them. inet_ntoa() is the
 * exception, see below. The lwip-internal
 * address headers it included stay, since kept code uses them (for example
 * ipaddr_aton() in bk_wifi_adapter.c). Its inet_addr_from_ip4addr-style
 * conversion macros have no user in the kept sources and are not carried.
 ****************************************************************************/

#ifndef LWIP_HDR_INET_H
#define LWIP_HDR_INET_H

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/ip6_addr.h"

#include <arpa/inet.h>
#include <netinet/in.h>

/* One exception: lwip's inet_ntoa() is a macro over any 4-byte address
 * lvalue (lwip/inet.h:141), and kept code relies on that. bk_netif.c:141
 * passes ip4_addr_t fields; bk_wifi_adapter.c:154 passes a void * variable,
 * printing the pointer value itself. NuttX's function takes a struct in_addr
 * by value and accepts neither, so these sources keep the lwip form.
 * NuttX code is unaffected: this header is only on the Armino include path.
 */

#undef inet_ntoa
#define inet_ntoa(addr) ip4addr_ntoa((const ip4_addr_t *)&(addr))

#endif /* LWIP_HDR_INET_H */
