/****************************************************************************
 * chips/bk7258/wifi_armino/lwip_os/lwip/netdb.h
 *
 * Wraps lwip-2.1.2/src/include/lwip/netdb.h for phase one
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). The DNS API is an OS
 * interface and comes from NuttX's <netdb.h>; the lwip copy redefined
 * struct hostent, struct addrinfo, the EAI_/AI_ constants and h_errno.
 * lwip's own switches turn those off; the rest (NETDB_ELEM_SIZE, used by
 * memp.c's pool table) is kept. Its only includer in the kept sources is
 * memp.c.
 ****************************************************************************/

#ifndef __BK7258_ARMINO_COMPAT_LWIP_NETDB_H
#define __BK7258_ARMINO_COMPAT_LWIP_NETDB_H

#include <netdb.h>

#define LWIP_DNS_API_DECLARE_H_ERRNO  0
#define LWIP_DNS_API_DEFINE_ERRORS    0
#define LWIP_DNS_API_DEFINE_FLAGS     0
#define LWIP_DNS_API_DECLARE_STRUCTS  0

#include_next <lwip/netdb.h>

#endif /* __BK7258_ARMINO_COMPAT_LWIP_NETDB_H */
