/*
 * BK7258 platform callbacks still referenced by the vendor capability tables.
 *
 * Policy: use NuttX equivalents where semantics are clear; return an explicit
 * unsupported error where a power/PM contract has not been ported. No fake
 * success for hardware control.
 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/clock.h>
#include <arm_internal.h>
#include <arch/chip/bk7258_clock.h>
#include <arch/chip/bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <modules/pm.h>

#include "driver/aon_rtc.h"
#include "os/os.h"

static bool g_phy_reinit;
/* The authority stores low-voltage callbacks by PM device ID.  A single slot
 * per sleep mode loses SARADC's callback when Wi-Fi registers its own MAC
 * callback, so retain the same ownership dimension even though NuttX does not
 * yet execute an Armino low-voltage transition. */
static pm_sleep_cb_t g_pm_sleep_enter[PM_MODE_DEFAULT][PM_DEV_ID_MAX];
static pm_sleep_cb_t g_pm_sleep_exit[PM_MODE_DEFAULT][PM_DEV_ID_MAX];
/* The CP has no owner for the external-32k mux yet.  Preserve registrations
 * faithfully for that future owner, but do not synthesize a source switch or
 * invoke a registered callback from this layer.  volatile makes this a real
 * retained state transition rather than an optimizer-elided success path. */
static volatile pm_cb_extern32k_cfg_t g_extern32k_cfg[PM_32K_MODULE_MAX];
static volatile bool g_extern32k_registered[PM_32K_MODULE_MAX];
static bool g_coex_wifi_open;

static pm_lpo_src_e bk7258_wifi_lpo_src_read(void)
{
  uint32_t source = getreg32(BK7258_AON_PMU_R41) &
                    BK7258_AON_PMU_R41_LPO_CONFIG_MASK;

  /* PM_LPO_SRC_DEFAULT is an enum sentinel, not a hardware encoding.  The
   * documented BK7258 field accepts DIVD, external 32K, or ROSC only. */
  if (source > (uint32_t)PM_LPO_SRC_ROSC)
    {
      return PM_LPO_SRC_ROSC;
    }

  return (pm_lpo_src_e)source;
}

/* bk_aon_rtc_get_current_tick() and bk_rtc_get_ms_tick_count() used to be
 * defined here.  Both now come from the authority AON RTC module imported under
 * chips/bk7258/aon_rtc/ (authority/middleware/driver/rtc/aon_rtc_driver_64bit.c
 * :1111 and :132), which additionally owns the counter enable/start sequence
 * that the read-only version here could not perform.
 *
 * The tick rate is unchanged by the switch.  This port returned 32.0f for a
 * non-X32K LPO source; the authority version returns s_aon_rtc_clock_freq/1000,
 * and with CONFIG_EXTERN_32K unset (authority BK7258 CP) s_aon_rtc_clock_freq
 * is AON_RTC_DEFAULT_CLOCK_FREQ == 32000, i.e. also 32.0f.  This matters
 * because libwifi.a's rwnxl_sleep() derives its poll timeout from this rate.
 * The authority value is a build-time default rather than a live PMU read, so
 * it no longer tracks a runtime LPO source change; the CP votes ROSC once
 * during bring-up (bk7258_wifi_lower.c) and does not switch afterwards, and the
 * authority firmware itself has the same static behavior via
 * CONFIG_DEFAULT_LPO_SRC.
 */

extern void hp_sys_hal_clk_pwr_ctrl(uint32_t dev, bool power_up);
extern void sys_drv_module_power_ctrl(power_module_name_t module,
                                      power_module_state_t power_state);

/* PM functions removed 2026-09-28: authoritative implementations
 * now come from pm/armino/pm.c (Armino components/bk_pm/pm.c migration).
 * See https://feishu.cn/docx/JMj8dZ4lDocze7xxti1cIHjlnjh */




/* Real register-backed power-state query, ported from pm.c:1309-1378 into
 * hal_port/hal_port_sys_int_power.c.  The former fixed
 * PM_POWER_MODULE_STATE_NONE return also contradicted hw_driver_shim's
 * fixed STATE_ON, though both slots report the same register. */
extern int32_t hp_bk_pm_module_power_state_get(pm_power_module_name_e module);
extern void hp_sys_hal_clk_pwr_ctrl(uint32_t dev, bool power_up);

/* Declared in hal_port/include/sys_driver.h, which this file does not include;
 * the PHY submodule vote below needs it to read back the power domain the
 * way pm.c:634 does. */
extern int32_t sys_drv_module_power_state_get(power_module_name_t module);

/* Keep the direct PHY module vote and PHY submodule votes in one ownership
 * state machine.  Armino's pm.c records PHY calibration when the direct PHY
 * vote succeeds; the following PHY_WIFI vote then observes an already-powered,
 * calibrated domain and must not replay phy_wakeup_reinit(). */

static uint32_t g_phy_submodule_state;
static uint32_t g_phy_calibration_state;
static uint32_t g_phy_off_modules;
static uint32_t g_phy_on_modules;
static spinlock_t g_phy_power_lock = SP_UNLOCKED;
static uint32_t g_bakp_submodule_state;
static spinlock_t g_bakp_power_lock = SP_UNLOCKED;


/* Real DVFS, ported from the Armino HAL (hal_port/hal_port_dvfs.c).
 * Replaces the former accept-and-log stub: that stub left the core voltage
 * at whatever the bootloader set, so the authoritative pairing of 120M with
 * vdddig 0.9V (sys_hal.c:663) never happened on our board. */
extern bk_err_t hp_pm_module_vote_cpu_freq(pm_dev_id_e module,
                                           pm_cpu_freq_e cpu_freq);








/* wifi_v2.c calls these two exported wrappers directly during bk_wifi_init().
 * Keep the upstream adapter's registration semantics while leaving actual
 * sleep-state transitions to the future NuttX PM owner.  In particular, do
 * not report a callback as executed: bk_pm_sleep_register_cb only records it.
 */



void bk_task_wdt_feed(void)
{
  /* NuttX watchdog ownership is outside this Wi-Fi port; no task watchdog is
   * armed by the current BK7258 CP configuration, so there is nothing to feed.
   */
}

void bk_system_dump(const char *func, const int line)
{
  (void)func;
  (void)line;
}

bool ate_is_enabled(void)
{
  return false;
}

bool ble_in_dut_mode(void)
{
  return false;
}

void coex_ictw_report_wifi_open_status(bool is_wifi_open)
{
  /* Armino's wifi_init.c only publishes this open/closed status.  Keep the
   * state for a future coex owner; this does not claim BT/PTA control. */
  g_coex_wifi_open = is_wifi_open;
}

/* libwifi.a was built with coex notification calls enabled.  This STA-only
 * build has no BT/PTA owner or registered recipient, so retain the ABI but do
 * not create policy, schedule BT work, or claim coexistence support. */
void coex_ictw_report_wifi_sleep_status(bool is_wifi_sleeping)
{
  (void)is_wifi_sleeping;
}

void coex_ictw_report_wifi_traffic(volatile uint32_t cntx_id,
                                   uint32_t level_idx)
{
  (void)cntx_id;
  (void)level_idx;
}

void coex_ictw_report_wifi_scan_status(volatile uint32_t cntx_id,
                                       bool is_scan_on)
{
  (void)cntx_id;
  (void)is_scan_on;
}

void coex_ictw_report_wifi_connecting_status(volatile uint32_t cntx_id,
                                             bool is_connecting)
{
  (void)cntx_id;
  (void)is_connecting;
}

void coex_ictw_report_wifi_connected_status(volatile uint32_t cntx_id,
                                            bool is_connected)
{
  (void)cntx_id;
  (void)is_connected;
}

bk_err_t cif_handle_bk_cmd_csi_info_ind(void *data)
{
  /* CSI/controller interface is not part of the STA MVP. Explicitly reject. */
  (void)data;
  return BK_ERR_NOT_SUPPORT;
}

bk_err_t cif_handle_bk_cmd_bcn_cc_ind(uint8_t *cc, uint8_t cc_len)
{
  /* Controller-side beacon country-code indication. This port has no
   * controller_if process; country code is configured through wireless_ops.
   */
  (void)cc;
  (void)cc_len;
  return BK_ERR_NOT_SUPPORT;
}

void shell_log_flush(void)
{
  /* NuttX stdio is synchronous in this port; no async shell ring to flush. */
}

void os_dump_memory_stats(uint32_t start_tick, uint32_t ticks_since_malloc,
                          const char *task)
{
  /* Debug-only API called by hostapd_intf when CONFIG_MEM_DEBUG is enabled.
   * NuttX heap accounting is already available through kmm_mallinfo(); keep
   * this hook side-effect free until it is routed to that diagnostic surface.
   */
  (void)start_tick;
  (void)ticks_since_malloc;
  (void)task;
}

void os_show_memory_config_info(void)
{
  /* Same debug-only rationale as os_dump_memory_stats(). */
}
