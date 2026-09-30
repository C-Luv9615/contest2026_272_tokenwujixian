/*
 * chips/bk7258/wifi/hal_port/include/bk_prelude.h
 *
 * Establishes Armino's global build contract for the vendored translation
 * units: definitions upstream injects globally rather than through a header
 * the vendored sources include themselves.
 *
 * How it reaches the vendored units -- two mechanisms, both deliberate:
 *
 * 1. The wpa_supplicant closure (the bk7258_wpa aux library in
 *    wifi/CMakeLists.txt) is force-included per file with the compiler's
 *    -include flag.  Per-source COMPILE_OPTIONS on `arch` cannot express
 *    this on CMake 3.22 (source properties only apply in the directory
 *    scope that created the target), which is why that closure builds as
 *    its own object library.  Do NOT add the prelude to the `arch` target's
 *    own compile options either: that would alter every NuttX architecture
 *    translation unit.
 *
 * 2. The Armino wifi/phy units carry an explicit `#include <bk_prelude.h>`
 *    prologue at the top of each file, next to the reasoning that unit
 *    needs.  Exceptions are documented in the file that needs them:
 *    skbuff.c must NOT include it (the prelude installs NuttX's one-argument
 *    spin_lock_irqsave() while that file uses the Linux two-argument form),
 *    and rwnx_rx.c additionally includes "driver.h".
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

#ifndef __BK7258_WIFI_HAL_PORT_BK_PRELUDE_H
#define __BK7258_WIFI_HAL_PORT_BK_PRELUDE_H

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
#    define CONFIG_TASK_WPAS_PRIO 5
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

/* Select wpa_supplicant's full diagnostics instead of Beken's reduced stand-in.
 *
 * This is the vendor's own switch, used in two places:
 *
 *   1. build_config.h:174 -- with CONFIG_WPA_LOG undefined the preprocessor
 *      evaluates `#if !CONFIG_WPA_LOG` as true and defines
 *      CONFIG_NO_STDOUT_DEBUG, CONFIG_NO_HOSTAPD_LOGGER and CONFIG_NO_WPA_MSG.
 *      Those collapse wpa_printf(), wpa_dbg(), wpa_msg() and the six
 *      wpa_hexdump*() variants into `do { } while (0)` (wpa_debug.h:38-57,
 *      :170-176) and keep the real implementations out of the image entirely --
 *      `nm` found no wpa_dbg, no wpa_msg and no _wpa_hexdump in the ELF.
 *
 *   2. Double-written call sites such as wpa_supplicant.c:3894-3902, which pick
 *      between the native `wpa_msg(wpa_s, MSG_INFO, ...)` and a `WPA_LOGD(...)`
 *      stand-in that routes through Beken's BK_LOG* macros.
 *
 * So the port has been running on the stand-in tier all along, and this flips
 * it to the full tier.  Nothing downstream is edited: wpa_debug.c and
 * wpa_debug.h remain byte-identical to the authority copies.
 *
 * WHY: the association path is diagnosed almost entirely through these macros,
 * so a failure there is silent by construction.  EVENT_ASSOC (events.c:5514)
 * can drop an association without a trace --
 *   if (wpa_s->disconnected)                  -> wpa_printf(MSG_INFO, "Ignore unexpected EVENT_ASSOC...")
 *   if (wpa_s->wpa_state == WPA_DISCONNECTED) -> wpa_printf(MSG_INFO, "Rx EVENT_ASSOC when WPAS is in disconnected state...")
 * -- and both lines were compiled out.  The board reaches `mm_set_vif_state ...
 * is_active=1, aid=0x1`, CONNECT_IND is confirmed delivered to the supplicant
 * queue, yet wpa_supplicant never leaves ASSOCIATING; telling "the handler never
 * ran" apart from "it ran and took a silent early return" is impossible while
 * its own log statements do not exist in the binary.
 *
 * The run-time level must stay MSG_DEBUG (main_supplicant.c:132).  Raising it to
 * MSG_INFO to cut volume would be a regression: after this flip `State: %s ->
 * %s` (wpa_supplicant.c:1056-1064) is emitted by wpa_dbg(MSG_DEBUG), so an INFO
 * threshold would discard the state transitions that are currently readable.
 *
 * DELIBERATE DEVIATION from the authority, which leaves CONFIG_WPA_LOG unset
 * (its sdkconfig.h has no such define, so cp/.../build_config.h:163 takes the
 * same suppressing branch).  Accepted because the authority does not need these
 * messages -- its association path works -- while ours is being brought up.  It
 * is strictly extra output, never less, and it changes no control flow: every
 * restored statement is a print.
 *
 * Cost, measured rather than assumed: the compiled WPA set (60 files listed in
 * CMakeLists.txt) holds 1398 wpa_printf/wpa_dbg call sites, so format strings
 * grow the image.  CP has room -- partition 0x165000 (1462272 B) against
 * 1084908 B in use, ~368 KiB free.
 *
 * Credentials stay protected through the upstream mechanism rather than a local
 * one: main_supplicant.c now passes wpa_debug_show_keys = 0, which makes every
 * wpa_hexdump_key()/wpa_hexdump_ascii_key() site print "[REMOVED]" in place of
 * the PMK/PTK/TK bytes (wpa_debug.c:113-127).
 *
 * REMOVE once the association path is closed: this is bring-up instrumentation,
 * and dropping it restores parity with the authority's logging.
 */

#ifndef CONFIG_WPA_LOG
#  define CONFIG_WPA_LOG 1
#endif

/* ---- Armino beken_genie sdkconfig.h alignment (2026-09-28) ----
 *
 * The 17 CONFIG_* symbols below gate #if branches in the Armino glue code
 * (rw_task.c, rw_msg_tx.c, rw_msg_rx.c, rw_ieee80211.c).  In the Armino
 * build they come from the project's generated sdkconfig.h; here the glue
 * sources reach them through this prelude (mechanism 2 at the top of this
 * file) and the wpa closure through the forced include (mechanism 1).
 *
 * Values must match bk_solution_ai/projects/beken_genie/build/bk7258/
 * beken_genie/bk7258/config/sdkconfig.h.  See the config-diff audit at
 * https://feishu.cn/docx/JMj8dZ4lDocze7xxti1cIHjlnjh for the full list.
 *
 * Do NOT move these to Kconfig: the Armino sources use bare #if CONFIG_FOO
 * (not #ifdef), which works identically whether the macro is 1 or undefined,
 * but the wpa Makefile -D flags and this prelude are the two mechanisms the
 * port already uses.  Kconfig would be a third, redundant path. */

#ifndef CONFIG_PM_V2
#  define CONFIG_PM_V2 1
#endif
#ifndef CONFIG_PM_ENABLE
#  define CONFIG_PM_ENABLE 1
#endif
#ifndef CONFIG_MCU_PS
#  define CONFIG_MCU_PS 1
#endif
#ifndef CONFIG_PM_ENTER_LV_TIME_OUT_PERIOD_MS
#  define CONFIG_PM_ENTER_LV_TIME_OUT_PERIOD_MS 30000
#endif
/* Full Armino beken_genie config alignment for PM/WiFi paths (2026-09-29).
 * These gate #if branches in pm.c, rw_msg_tx.c, rw_msg_rx.c, rw_task.c.
 * See https://feishu.cn/docx/JMj8dZ4lDocze7xxti1cIHjlnjh config diff audit. */
#ifndef CONFIG_MCU_PS
#  define CONFIG_MCU_PS 1
#endif
#ifndef CONFIG_WIFI_ENABLE
#  define CONFIG_WIFI_ENABLE 1
#endif
#ifndef CONFIG_GENERAL_DMA
#  define CONFIG_GENERAL_DMA 1
#endif
#ifndef CONFIG_CPU_CNT
#  define CONFIG_CPU_CNT 3
#endif
#ifndef CONFIG_AON_RTC
#  define CONFIG_AON_RTC 1
#endif
#ifndef CONFIG_BAKP_POWER_DOMAIN_PM_CONTROL
#  define CONFIG_BAKP_POWER_DOMAIN_PM_CONTROL 1
#endif
#ifndef CONFIG_RWNX_QOS_MSDU
#  define CONFIG_RWNX_QOS_MSDU 1
#endif
#ifndef CONFIG_WIFI_CORE_TASK_PRIO
#  define CONFIG_WIFI_CORE_TASK_PRIO 2
#endif
#ifndef CONFIG_WIFI_CORE_TASK_STACK_SIZE
#  define CONFIG_WIFI_CORE_TASK_STACK_SIZE 2048
#endif
#ifndef CONFIG_WIFI_KMSG_TASK_PRIO
#  define CONFIG_WIFI_KMSG_TASK_PRIO 3
#endif
#ifndef CONFIG_WIFI_KMSG_TASK_STACK_SIZE
#  define CONFIG_WIFI_KMSG_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_PM_PSRAM_FORCE_ON
#  define CONFIG_PM_PSRAM_FORCE_ON 1
#endif
#ifndef CONFIG_PM_SUPER_DEEP_SLEEP
#  define CONFIG_PM_SUPER_DEEP_SLEEP 1
#endif
#ifndef CONFIG_STA_AUTO_RECONNECT
#  define CONFIG_STA_AUTO_RECONNECT 1
#endif
#ifndef CONFIG_MSDU_RESV_TAIL_LENGTH
#  define CONFIG_MSDU_RESV_TAIL_LENGTH 16
#endif
#ifndef CONFIG_MAC_PHY_BYPASS
#  define CONFIG_MAC_PHY_BYPASS 1
#endif
#ifndef CONFIG_RWNX_FAST_CONNECT
#  define CONFIG_RWNX_FAST_CONNECT 1
#endif
#ifndef CONFIG_STA_PS
#  define CONFIG_STA_PS 1
#endif
#ifndef CONFIG_WIFI4
#  define CONFIG_WIFI4 1
#endif
#ifndef CONFIG_RWNX_SW_TXQ
#  define CONFIG_RWNX_SW_TXQ 1
#endif
#ifndef CONFIG_RWNX_TD
#  define CONFIG_RWNX_TD 1
#endif
#ifndef CONFIG_SPECIAL_TX_TYPE
#  define CONFIG_SPECIAL_TX_TYPE 1
#endif
#ifndef CONFIG_WIFI_VNET_CONTROLLER
#  define CONFIG_WIFI_VNET_CONTROLLER 1
#endif
#ifndef CONFIG_MINIMUM_SCAN_RESULTS
#  define CONFIG_MINIMUM_SCAN_RESULTS 1
#endif
#ifndef CONFIG_WIFI_SCAN_COUNTRY_CODE
#  define CONFIG_WIFI_SCAN_COUNTRY_CODE 1
#endif
#ifndef CONFIG_WIFI_AUTO_COUNTRY_CODE
#  define CONFIG_WIFI_AUTO_COUNTRY_CODE 1
#endif
#ifndef CONFIG_WIFI6
#  define CONFIG_WIFI6 1
#endif
#ifndef CONFIG_PMF
#  define CONFIG_PMF 1
#endif
#ifndef CONFIG_MAC_SFRAME_SOFTWARE_RETRY
#  define CONFIG_MAC_SFRAME_SOFTWARE_RETRY 1
#endif
/* PSRAM is folded into the NuttX heap, so the Armino psram_* allocator
 * names map onto the standard heap primitives. */
#ifndef psram_zalloc
#  define psram_zalloc(size)       os_zalloc(size)
#endif
#ifndef psram_malloc
#  define psram_malloc(size)       os_malloc(size)
#endif
#ifndef psram_free
#  define psram_free(ptr)          os_free(ptr)
#endif
#ifndef bk_psram_realloc
#  define bk_psram_realloc(ptr, sz) os_realloc(ptr, sz)
#endif

#ifndef CONFIG_PSRAM_AS_SYS_MEMORY
#  define CONFIG_PSRAM_AS_SYS_MEMORY 1
#endif
#ifndef CONFIG_CONTROLLER_RX_DIRECT_PSH
#  define CONFIG_CONTROLLER_RX_DIRECT_PSH 1
#endif
#ifndef CONFIG_CONTROLLER_WAR
#  define CONFIG_CONTROLLER_WAR 1
#endif

/* pm.c config gates -- matched to Armino beken_genie sdkconfig.h (2026-09-29). */
#ifndef CONFIG_SYSTEM_CTRL
#  define CONFIG_SYSTEM_CTRL 1
#endif
#ifndef CONFIG_FLASH
#  define CONFIG_FLASH 1
#endif
#ifndef CONFIG_CKMN
#  define CONFIG_CKMN 1
#endif
#ifndef CONFIG_INT_WDT
#  define CONFIG_INT_WDT 1
#endif
#ifndef CONFIG_GPIO_WAKEUP_SUPPORT
#  define CONFIG_GPIO_WAKEUP_SUPPORT 1
#endif
#ifndef CONFIG_PM_LV_WDT_PROTECTION
#  define CONFIG_PM_LV_WDT_PROTECTION 1
#endif
#ifndef CONFIG_AON_WDT
#  define CONFIG_AON_WDT 1
#endif
#ifndef CONFIG_PSRAM
#  define CONFIG_PSRAM 1
#endif
#ifndef CONFIG_GPIO_RETENTION_SUPPORT
#  define CONFIG_GPIO_RETENTION_SUPPORT 1
#endif
#ifndef CONFIG_SOC_BK7236XX
#  define CONFIG_SOC_BK7236XX 1
#endif
#ifndef CONFIG_PM_LV_GPIO_WAKEUP_CALL_MAC_CB
#  define CONFIG_PM_LV_GPIO_WAKEUP_CALL_MAC_CB 1
#endif

#endif /* __BK7258_WIFI_GLUE_BK_PRELUDE_H */
