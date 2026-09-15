/*
 * chips/bk7258/wifi/glue/include/bk_prelude.h
 *
 * Force-included (-include) ahead of every vendored translation unit, to
 * reproduce definitions that upstream injects globally rather than through a
 * header the vendored sources include themselves.
 *
 * Why a prelude instead of a normal header: upstream reaches BIT() via
 * common/bk_include.h -> soc/soc.h:85. We deliberately do not vendor
 * soc/soc.h (registers come from the team chip layer), and the headers that
 * need BIT() do not go through bk_include.h anyway --
 * wpa_supplicant/src/common/defs.h has no includes at all, and
 * rwnx_txq.h only includes sys_config.h/skbuff.h/bk_list.h. Defining BIT()
 * in bk_generic.h therefore would not reach them.
 *
 * Keep this file tiny and free of NuttX kernel headers: it lands in front of
 * every vendored source, so anything here is maximally load-bearing.
 */

#ifndef __BK7258_WIFI_GLUE_BK_PRELUDE_H
#define __BK7258_WIFI_GLUE_BK_PRELUDE_H

/* Armino's WPA port assumes these are supplied by the global build
 * environment before utils/common.h is parsed.  Keep the definitions in the
 * force-included prelude so every vendored translation unit sees the same ABI
 * and byte order, independent of its first include. */

#include <common/bk_typedef.h>
#include <components/system.h>
#include <os/os.h>
#include <wireless_ioctl_compat.h>
#include <wpa_compat/ip_addr.h>

#ifndef BK_SUPPLICANT
#  define BK_SUPPLICANT 1
#endif
#ifndef BK_MAC
#  define BK_MAC 1
#endif
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __LITTLE_ENDIAN
#endif

#ifndef __packed
#  define __packed __attribute__((__packed__))
#endif
#ifndef __PACKED
#  define __PACKED __attribute__((__packed__))
#endif

/* Armino's wifi_spinlock.h redefines NuttX lock macros with incompatible
 * arity. Include NuttX's real type/API, then consume the vendor header guard
 * before any quoted include can install its dummy replacements. */
#include <nuttx/spinlock.h>
#ifndef __SPIN_LOCK_H_
#  define __SPIN_LOCK_H_
#endif
#ifndef spin_lock_bh
#  define spin_lock_bh(lock)   spin_lock(lock)
#endif
#ifndef spin_unlock_bh
#  define spin_unlock_bh(lock) spin_unlock(lock)
#endif

/* Guarded: NuttX has its own BIT() in <nuttx/bits.h>, and wpa_supplicant's
 * src/utils/common.h:355 also defines it under the same guard. Value matches
 * upstream soc.h:85.
 */

#ifndef BIT
#  define BIT(i) (1 << (i))
#endif

/* likely/unlikely: NuttX spells these predict_true/predict_false
 * (include/nuttx/compiler.h:251), so map onto those rather than re-deriving
 * __builtin_expect -- that way a toolchain without the builtin degrades exactly
 * as NuttX intends. Used by rwnx_tx.c, rw_msdu.c and rwnx_misc.c.
 */

#include <nuttx/compiler.h>
#include <nuttx/nuttx.h>

#ifndef CONFIG_MSDU_RESV_HEAD_LENGTH
#  define CONFIG_MSDU_RESV_HEAD_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_HEAD_LENGTH
#endif
#ifndef CONFIG_MSDU_RESV_DESC_LENGTH
#  define CONFIG_MSDU_RESV_DESC_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_DESC_LENGTH
#endif

#ifndef MICROSECONDS
#  define MICROSECONDS 1000u
#endif

#ifndef os_sram_zalloc
#  define os_sram_zalloc(size) os_zalloc(size)
#endif

#ifndef likely
#  define likely(x)   predict_true(x)
#endif
#ifndef unlikely
#  define unlikely(x) predict_false(x)
#endif

#ifndef NULLPTR
#  define NULLPTR ((void *)0)
#endif
#ifndef CONFIG_TASK_WPAS_PRIO
#  ifdef CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY
#    define CONFIG_TASK_WPAS_PRIO CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY
#  else
     /* The runtime probe defconfig pins this provider priority to 100. */
#    define CONFIG_TASK_WPAS_PRIO 100
#  endif
#endif

/* BK_ASSERT reaches the vendored sources the same way BIT() does: the files
 * using it (bk_wifi_adapter.c, rw_ieee80211.c, rw_msg_rx.c, rw_msg_tx.c,
 * rwnx_rx.c, ...) never include common/bk_assert.h, so upstream must be
 * injecting it. Without it here the call parses as an implicit function and
 * fails at link, which is exactly what the first real link reported.
 *
 * Mapped to NuttX DEBUGASSERT, i.e. it follows CONFIG_DEBUG_ASSERTIONS: on a
 * release build the expression is evaluated for side effects and discarded,
 * matching NuttX's own contract rather than inventing a third behaviour.
 */

#include <assert.h>

#ifndef BK_ASSERT
#  define BK_ASSERT(exp)         DEBUGASSERT(exp)
#endif
#ifndef BK_ASSERT_EX
#  define BK_ASSERT_EX(exp, ...) DEBUGASSERT(exp)
#endif

/* getreg32 is a NuttX macro in arch/arm/src/common/arm_internal.h, not a
 * function; without the declaration in scope our own shims turned it into an
 * implicit call. Its sibling modifyreg32() is a real function, which is why only
 * getreg32 showed up undefined.
 */

#include <arm_internal.h>

#ifndef getreg32
#  define getreg32(a) (*(volatile uint32_t *)(a))
#endif

/* The vendor glue's rwm_proto.h owns its historical `struct ethhdr` layout
 * (dest/src arrays + proto). NuttX <netinet/if_ether.h> defines a different
 * ethhdr (embedded eth_addr structs), and a forced WPA IPv4/socket include can
 * otherwise make both appear in one translation unit. The vendor code does not
 * need NuttX's ethhdr type here, so keep that header from being re-entered in
 * vendor translation units; the NuttX netdev path uses its own headers in its
 * own translation units.
 */

#ifndef __INCLUDE_NETINET_IF_ETHER_H
#  define __INCLUDE_NETINET_IF_ETHER_H 1
#endif

#endif /* __BK7258_WIFI_GLUE_BK_PRELUDE_H */
