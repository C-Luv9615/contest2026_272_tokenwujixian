/****************************************************************************
 * chips/bk7258/wifi_armino/lwip_os/lwip/stats.h
 *
 * Wraps lwip-2.1.2/src/include/lwip/stats.h for phase one
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). Its struct stats_proto
 * has a member named opterr, which NuttX's <unistd.h> defines as a macro
 * (*(getopterrp())). No kept source reads that member, so the lwip header
 * is used unchanged with the macro suspended around it.
 *
 * <unistd.h> is included first: lwip/arch.h includes it during the
 * include_next below, and its guard must already be set so opterr is not
 * defined again inside the suspended region.
 ****************************************************************************/

#ifndef __BK7258_ARMINO_COMPAT_LWIP_STATS_H
#define __BK7258_ARMINO_COMPAT_LWIP_STATS_H

#include <unistd.h>

#pragma push_macro("opterr")
#undef opterr
#include_next <lwip/stats.h>
#pragma pop_macro("opterr")

#endif /* __BK7258_ARMINO_COMPAT_LWIP_STATS_H */
