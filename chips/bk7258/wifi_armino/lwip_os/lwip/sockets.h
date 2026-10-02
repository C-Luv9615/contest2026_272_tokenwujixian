/****************************************************************************
 * chips/bk7258/wifi_armino/lwip_os/lwip/sockets.h
 *
 * Replaces lwip-2.1.2/src/include/lwip/sockets.h for phase one
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). The socket API is an OS
 * interface: lwip is excluded and NuttX's own TCP/IP stack serves it. The
 * lwip copy redefined socklen_t, sa_family_t, struct sockaddr, struct iovec
 * and friends against NuttX's, and its LWIP_COMPAT_SOCKETS macros renamed
 * socket() and the rest to lwip_*().
 *
 * Kept sources (bk_wifi_adapter.c, iperf.c, cli_wifi.c) call the BSD names
 * directly, which now reach NuttX. This lwip has no
 * LWIP_SOCKET_EXTERNAL_HEADERS switch, hence a replacement header; it keeps
 * the lwip guard so the original can never be pulled in as well.
 ****************************************************************************/

#ifndef LWIP_HDR_SOCKETS_H
#define LWIP_HDR_SOCKETS_H

/* The non-socket lwip headers the original included. lwip/errno.h is left
 * out: errno is NuttX's (S2.7).
 */

#include "lwip/opt.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/inet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

/* The two names LWIP_COMPAT_SOCKETS provided that POSIX does not. */

#define closesocket(s)               close(s)
#define ioctlsocket(s, cmd, argp)    ioctl(s, cmd, argp)

#endif /* LWIP_HDR_SOCKETS_H */
