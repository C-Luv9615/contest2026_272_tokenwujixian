/*
 * BK7258 platform callbacks still referenced by the vendor capability tables.
 *
 * Policy: use NuttX equivalents where semantics are clear; return an explicit
 * unsupported error where a power/PM contract has not been ported. No fake
 * success for hardware control.
 */

#include <nuttx/config.h>
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
static pm_sleep_cb_t g_pm_sleep_enter[PM_MODE_DEFAULT];
static pm_sleep_cb_t g_pm_sleep_exit[PM_MODE_DEFAULT];
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

uint64_t bk_aon_rtc_get_current_tick(aon_rtc_id_t id)
{
  uint32_t low;
  uint32_t high;

  /* BK7258 has one AON RTC unit (ID 0).  Do not initialize it from this
   * accessor: the Armino init sequence resets the shared counter and changes
   * interrupt/compare state.  This provider is deliberately read-only. */
  if (id != 0)
    {
      return 0;
    }

  /* Match the BK7258 64-bit Armino HAL snapshot sequence.  Counter low/high
   * are independently visible MMIO words; reread until the pair is stable so
   * a carry cannot produce a torn timestamp. */
  do
    {
      low = getreg32(BK7258_AON_RTC_COUNTER_LO);
      high = getreg32(BK7258_AON_RTC_COUNTER_HI);
    }
  while (getreg32(BK7258_AON_RTC_COUNTER_LO) != low ||
         getreg32(BK7258_AON_RTC_COUNTER_HI) != high);

  return ((uint64_t)high << 32) | low;
}

float bk_rtc_get_ms_tick_count(void)
{
  /* Armino's API name is misleading: this is the AON RTC tick rate expressed
   * in ticks per millisecond, not a running millisecond timestamp.  Read the
   * live PMU source selection rather than asserting that CP owns external 32K.
   * The vendor uses 32 kHz for both ROSC and DIVD, while X32K is 32768 Hz. */
  return bk7258_wifi_lpo_src_read() == PM_LPO_SRC_X32K ? 32.768f : 32.0f;
}

bk_err_t bk_pm_clock_ctrl(pm_dev_clk_e module, pm_dev_clk_pwr_e clock_state)
{
  bool enable;

  if (clock_state == PM_CLK_CTRL_PWR_UP)
    {
      enable = true;
    }
  else if (clock_state == PM_CLK_CTRL_PWR_DOWN)
    {
      enable = false;
    }
  else
    {
      return BK_ERR_PARAM;
    }

  if (module == PM_CLK_ID_MAC)
    {
      return bk7258_mac_clock(enable) == OK ? BK_OK : BK_FAIL;
    }
  else if (module == PM_CLK_ID_PHY)
    {
      return bk7258_phy_clock(enable) == OK ? BK_OK : BK_FAIL;
    }

  return BK_ERR_NOT_SUPPORT;
}

pm_lpo_src_e bk_pm_lpo_src_get(void)
{
  /* The PMU R41 field is the live BK7258 LPO source.  This query does not
   * claim ownership of the mux or alter its selection. */
  return bk7258_wifi_lpo_src_read();
}

int32 bk_pm_module_power_state_get(pm_power_module_name_e module)
{
  (void)module;
  return (int32)PM_POWER_MODULE_STATE_NONE;
}

bk_err_t bk_pm_module_vote_cpu_freq(pm_dev_id_e module, pm_cpu_freq_e cpu_freq)
{
  (void)module;
  (void)cpu_freq;
  return BK_ERR_NOT_SUPPORT;
}

bk_err_t bk_pm_module_vote_power_ctrl(pm_power_module_name_e module,
                                       pm_power_module_state_e power_state)
{
  bool enable;

  if (power_state == PM_POWER_MODULE_STATE_ON)
    {
      enable = true;
    }
  else if (power_state == PM_POWER_MODULE_STATE_OFF)
    {
      enable = false;
    }
  else
    {
      return BK_ERR_PARAM;
    }

  if (module == PM_POWER_MODULE_NAME_WIFIP_MAC)
    {
      return bk7258_mac_power(enable) == OK ? BK_OK : BK_FAIL;
    }
  else if (module == PM_POWER_MODULE_NAME_PHY)
    {
      return bk7258_phy_power(enable) == OK ? BK_OK : BK_FAIL;
    }

  return BK_ERR_NOT_SUPPORT;
}

bk_err_t bk_pm_module_vote_sleep_ctrl(pm_sleep_module_name_e module,
                                       uint32_t sleep_state,
                                       uint32_t sleep_time)
{
  (void)module;
  (void)sleep_state;
  (void)sleep_time;
  return BK_ERR_NOT_SUPPORT;
}

void bk_pm_phy_reinit_flag_clear(void)
{
  g_phy_reinit = false;
}

bool bk_pm_phy_reinit_flag_get(void)
{
  return g_phy_reinit;
}

bk_err_t bk_pm_sleep_register_cb(pm_sleep_mode_e sleep_mode,
                                  pm_dev_id_e dev_id,
                                  pm_cb_conf_t *enter_config,
                                  pm_cb_conf_t *exit_config)
{
  if (sleep_mode >= PM_MODE_DEFAULT)
    {
      return BK_ERR_PARAM;
    }

  /* NuttX currently has no mapped BK sleep transition.  Retaining these
   * source-backed registrations is nevertheless required so a later owned
   * transition driver can invoke exactly the callback and opaque argument
   * supplied by the Wi-Fi/PHY source. */
  if (enter_config != NULL)
    {
      g_pm_sleep_enter[sleep_mode].id = dev_id;
      g_pm_sleep_enter[sleep_mode].cfg = *enter_config;
    }

  if (exit_config != NULL)
    {
      g_pm_sleep_exit[sleep_mode].id = dev_id;
      g_pm_sleep_exit[sleep_mode].cfg = *exit_config;
    }

  return BK_OK;
}

/* wifi_v2.c calls these two exported wrappers directly during bk_wifi_init().
 * Keep the upstream adapter's registration semantics while leaving actual
 * sleep-state transitions to the future NuttX PM owner.  In particular, do
 * not report a callback as executed: bk_pm_sleep_register_cb only records it.
 */
bk_err_t bk_pm_sleep_register_wrapper(void *config_cb)
{
  pm_cb_conf_t enter_config_wifi = {NULL, NULL};

  enter_config_wifi.cb = config_cb;
  return bk_pm_sleep_register_cb(PM_MODE_DEEP_SLEEP, PM_DEV_ID_MAC,
                                 &enter_config_wifi, NULL);
}

bk_err_t bk_pm_low_voltage_register_wrapper(void *config_cb)
{
  pm_cb_conf_t wifi_exit_config = {NULL, NULL};

  wifi_exit_config.cb = config_cb;
  return bk_pm_sleep_register_cb(PM_MODE_LOW_VOLTAGE, PM_DEV_ID_MAC, NULL,
                                 &wifi_exit_config);
}

bk_err_t pm_extern32k_register_cb(pm_cb_extern32k_cfg_t *cfg)
{
  if (cfg == NULL || cfg->cb_module >= PM_32K_MODULE_MAX ||
      cfg->cb_func == NULL)
    {
      return BK_ERR_PARAM;
    }

  g_extern32k_cfg[cfg->cb_module] = *cfg;
  g_extern32k_registered[cfg->cb_module] = true;
  return BK_OK;
}

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

bk_err_t bk_sensor_set_current_temperature(float temperature)
{
  (void)temperature;
  return BK_ERR_NOT_SUPPORT;
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
