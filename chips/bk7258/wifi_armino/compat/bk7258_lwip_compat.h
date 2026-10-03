/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_lwip_compat.h
 *
 * Forced into the kept lwip units (docs/compose/spec/bk7258-wifi-native.md,
 * S2.7), which stay unchanged. Two of them need it:
 *
 *  - stats.c reads proto->opterr (stats.c:76), and NuttX's <unistd.h>
 *    defines opterr as a macro. lwip_os/lwip/stats.h only suspends it
 *    around the header; stats.c's body needs it gone, and uses no getopt.
 *  - pbuf.c uses pbuf_debug_flag unconditionally (pbuf.c:181, 186), but
 *    Beken defines it inside the TCP out-of-order branch (pbuf.c:116),
 *    which PBUF_POOL_FREE_OOSEQ=0 compiles out. Defined in
 *    bk7258_lwip_sys.c.
 ****************************************************************************/

#ifndef __BK7258_ARMINO_COMPAT_LWIP_COMPAT_H
#define __BK7258_ARMINO_COMPAT_LWIP_COMPAT_H

#include <stdint.h>
#include <unistd.h>

#undef opterr

extern uint32_t pbuf_debug_flag;

#endif /* __BK7258_ARMINO_COMPAT_LWIP_COMPAT_H */
