/*
 * chips/bk7258/wifi/glue/analog_shim.c
 *
 * Analog / calibration surface the vendored bk_phy_adapter.c registers into the
 * PHY capability table: crystal fine-tune, DPLL calibration, bandgap trim, core
 * and LDO voltage selects, the ADC divider, temperature and voltage monitoring.
 *
 * ==== Why none of this is a no-op ====
 *
 * Upstream reaches the analog register file through a latched write sequence
 * (sys_set_ana_reg_bit(), see the note in glue/include/sys_ll.h), not a single
 * store, and that sequencing layer is not ported for BK7258. Most of these
 * functions return void, so they have no way to tell libbk_phy.a that nothing
 * happened.
 *
 * A silent no-op would therefore report success by omission: the PHY would
 * proceed believing its analog front end was configured -- core voltage set,
 * bandgap trimmed, crystal pulled -- while no register was touched. On an RF
 * part that does not fail loudly; it transmits out of spec.
 *
 * So every unported entry point here logs an error naming itself, once per call
 * site per boot, and touches no hardware. Deliberately:
 *   - not silent, so bring-up sees it in the log rather than as mysterious RF
 *     behaviour;
 *   - not a panic/assert, so bring-up can still reach a state where the log is
 *     readable -- an early abort inside PHY init would hide everything after it;
 *   - rate-limited, because some of these are called from calibration loops and
 *     an unthrottled log would drown the console it is meant to inform.
 *
 * Functions that DO have an error channel (bk_err_t / int) also return failure.
 *
 * Everything in this file is a milestone-4 (RF bring-up) item, not a leftover:
 * see ABI_AUDIT.md for the blocking list.
 */

#include <nuttx/config.h>
#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_typedef.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <arch/chip/bk7258_sysctrl.h>

#include "driver/adc.h"
#include "sys_ll.h"
#include "sys_driver.h"
#include "bk_saradc.h"
#include "drv_model_pub.h"
#include "bk_private/bk_phy.h"
#include "temp_detect.h"
#include "temp_detect_pub.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define ANALOG_TAG "bk_analog"
#define ANA_FIELD_MASK(_width, _shift) \
  (((UINT32_C(1) << (_width)) - UINT32_C(1)) << (_shift))

/* One complaint per distinct entry point per boot. The vendored PHY calls some
 * of these from calibration loops; without this the log is unusable.
 */

#define ANALOG_UNPORTED(_name)                                             \
  do                                                                       \
    {                                                                      \
      static bool _warned;                                                 \
      if (!_warned)                                                        \
        {                                                                  \
          _warned = true;                                                  \
          BK_LOGE(ANALOG_TAG,                                              \
                  "%s: BK7258 analog register access not ported; "         \
                  "RF calibration is NOT applied\n", (_name));             \
        }                                                                  \
    }                                                                      \
  while (0)

/****************************************************************************
 * Public data
 ****************************************************************************/

/* Shared state byte the vendored adapter writes (bk_phy_adapter.c:53-56).
 * Upstream lives in its SARADC driver; owned here since that driver is absent.
 */

UINT8 g_saradc_flag = 0x0;

/* The vendor temperature daemon is asynchronous: it samples immediately,
 * then every second for thirty samples, then every fifteen seconds. Keep the
 * SARADC work and the PHY temperature-compensation callback out of ISR context
 * on LPWORK: it may wait for an ADC conversion and must not consume the
 * high-priority HISR/timer bottom-half worker. Serialize its lifecycle with a
 * single BK7258-owned state object. */
struct bk7258_tempd_state
{
  struct work_s work;
  spinlock_t lock;
  bool active;
  uint16_t last_temperature;
  uint32_t sample_count;
};

static struct bk7258_tempd_state g_tempd =
{
  .lock = SP_UNLOCKED,
};

static void bk7258_tempd_worker(void *arg);

/****************************************************************************
 * sys_ll analog register accessors
 *
 * The BK7258 chip layer supplies the matching latched transaction. These are
 * field setters, not whole-register writes: each update preserves unrelated
 * bits and serializes read-modify-write with the SPI completion poll.
 ****************************************************************************/

static void bk7258_analog_set_field(const char *name, unsigned int reg,
                                    uint32_t mask, unsigned int shift,
                                    uint32_t value)
{
  int ret = bk7258_analog_update_bits(reg, mask, value << shift);

  if (ret < 0)
    {
      BK_LOGE(ANALOG_TAG, "%s: analog REG%u update failed=%d\n",
              name, reg, ret);
    }
}

void sys_ll_set_ana_reg5_adc_div(uint32_t v)
{
  bk7258_analog_set_field(__func__, 5, ANA_FIELD_MASK(2, 10), 10, v);
}

uint32_t sys_ll_get_ana_reg5_adc_div(void)
{
  uint32_t value;

  if (bk7258_analog_read(5, &value) < 0)
    {
      BK_LOGE(ANALOG_TAG, "%s: analog REG5 read failed\n", __func__);
      return 0;
    }

  return (value >> 10) & UINT32_C(0x3);
}

void sys_ll_set_ana_reg8_iocurlim(uint32_t v)
{
  bk7258_analog_set_field(__func__, 8, ANA_FIELD_MASK(1, 15), 15, v);
}

void sys_ll_set_ana_reg8_ioldo_lp(uint32_t v)
{
  bk7258_analog_set_field(__func__, 8, ANA_FIELD_MASK(1, 0), 0, v);
}

void sys_ll_set_ana_reg8_violdosel(uint32_t v)
{
  bk7258_analog_set_field(__func__, 8, ANA_FIELD_MASK(3, 12), 12, v);
}

void sys_ll_set_ana_reg9_spi_latch1v(uint32_t v)
{
  bk7258_analog_set_field(__func__, 9, ANA_FIELD_MASK(1, 9), 9, v);
}

void sys_ll_set_ana_reg9_vcorehsel(uint32_t v)
{
  bk7258_analog_set_field(__func__, 9, ANA_FIELD_MASK(4, 16), 16, v);
}

void sys_ll_set_ana_reg10_iobyapssen(uint32_t v)
{
  bk7258_analog_set_field(__func__, 10, ANA_FIELD_MASK(1, 19), 19, v);
}

void sys_ll_set_ana_reg11_aldosel(uint32_t v)
{
  bk7258_analog_set_field(__func__, 11, ANA_FIELD_MASK(1, 31), 31, v);
}

void sys_ll_set_ana_reg12_dldosel(uint32_t v)
{
  bk7258_analog_set_field(__func__, 12, ANA_FIELD_MASK(1, 31), 31, v);
}

/****************************************************************************
 * sys_drv analog / calibration -- UNPORTED
 ****************************************************************************/

uint32_t sys_drv_analog_set_xtalh_ctune(uint32_t param)
{
  int ret;

  /* BK7258 SDK sys_hal_set_xtalh_ctune() selects ANA_REG2[7:0]. */
  ret = bk7258_analog_update_bits(2, UINT32_C(0xff), param);
  if (ret < 0)
    {
      BK_LOGE(ANALOG_TAG, "%s: ANA2 xtalh_ctune update failed=%d\n",
              __func__, ret);
      return (uint32_t)BK_FAIL;
    }

  return BK_OK;
}

uint32_t sys_drv_cali_dpll(uint32_t param)
{
  /* HAL-alignment: call the verbatim port of the authoritative
   * sys_wifi_driver.c sequence (hp_sys_drv_cali_dpll) instead of the
   * hand-written approximation in bk7258_sysctrl.c. */
  extern uint32_t hp_sys_drv_cali_dpll(uint32_t param);

  return hp_sys_drv_cali_dpll(param);
}

uint32_t sys_drv_get_bgcalm(void)
{
  ANALOG_UNPORTED("sys_drv_get_bgcalm");
  return 0;
}

uint32_t sys_drv_set_bgcalm(uint32_t param)
{
  (void)param;
  ANALOG_UNPORTED("sys_drv_set_bgcalm");
  return (uint32_t)BK_FAIL;
}

uint32_t sys_drv_get_vdd_value(void)
{
  ANALOG_UNPORTED("sys_drv_get_vdd_value");
  return 0;
}

uint32_t sys_drv_set_vdd_value(uint32_t param)
{
  (void)param;
  ANALOG_UNPORTED("sys_drv_set_vdd_value");
  return (uint32_t)BK_FAIL;
}

void sys_drv_set_ana_cb_cal_manu(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, UINT32_C(1) << 23, 23, value);
}

void sys_drv_set_ana_cb_cal_trig(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, UINT32_C(1) << 22, 22, value);
}

void sys_drv_set_ana_cb_cal_manu_val(uint32_t value)
{
  bk7258_analog_set_field(__func__, 5, ANA_FIELD_MASK(5, 27), 27, value);
}

void sys_drv_set_ana_ioldo_lp(uint32_t value)
{
  /* Authoritative BK7258 sys_hal_set_ioldo_lp: the IO-LDO low-power flag
   * maps onto the ANA_REG8 ioldo_lp bit. */
  sys_ll_set_ana_reg8_ioldo_lp(!!value);
}

void sys_drv_set_ana_reg11_apfms(uint32_t value)
{
  bk7258_analog_set_field(__func__, 11, ANA_FIELD_MASK(5, 5), 5, value);
}

void sys_drv_set_ana_reg12_dpfms(uint32_t value)
{
  bk7258_analog_set_field(__func__, 12, ANA_FIELD_MASK(5, 8), 8, value);
}

void sys_drv_module_power_ctrl(power_module_name_t module,
                               power_module_state_t power_state)
{
  (void)module;
  (void)power_state;
  ANALOG_UNPORTED("sys_drv_module_power_ctrl");
}

void sys_hal_enter_low_analog(void)
{
  ANALOG_UNPORTED("sys_hal_enter_low_analog");
}

void sys_hal_exit_low_analog(void)
{
  ANALOG_UNPORTED("sys_hal_exit_low_analog");
}

/****************************************************************************
 * Legacy driver-model control -- UNPORTED
 ****************************************************************************/

UINT32 ddev_control(DD_HANDLE handle, UINT32 cmd, void *param)
{
  /* The vendored PHY reaches SCTRL / ICU / BLE blocks through this. Refusing is
   * right: the alternative is dispatching commands we have not mapped.
   */

  (void)handle;
  (void)cmd;
  (void)param;
  ANALOG_UNPORTED("ddev_control");
  return (UINT32)BK_FAIL;
}

/****************************************************************************
 * SARADC conversion
 ****************************************************************************/

float saradc_calculate(UINT16 adc_val)
{
  /* The PHY receives the per-device AON ADC/bias trim directly through its
   * capability table.  The formula that combines those trim fields with a raw
   * SARADC count is still not established for BK7258, so do not invent a
   * voltage conversion or apply the trim twice here.
   */

  (void)adc_val;
  ANALOG_UNPORTED("saradc_calculate");
  return 0.0f;
}

/****************************************************************************
 * Temperature / voltage monitoring
 ****************************************************************************/

int temp_detect_init(uint32_t init_val)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_tempd.lock);
  if (g_tempd.active)
    {
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      return BK_OK;
    }

  g_tempd.active = true;
  g_tempd.last_temperature = (uint16_t)init_val;
  g_tempd.sample_count = 0;
  ret = work_queue(LPWORK, &g_tempd.work, bk7258_tempd_worker, &g_tempd, 0);
  if (ret < 0)
    {
      g_tempd.active = false;
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      BK_LOGE(ANALOG_TAG, "%s: schedule failed=%d\n", __func__, ret);
      return BK_FAIL;
    }

  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

int temp_detect_deinit(void)
{
  irqstate_t flags;
  bool active;

  flags = spin_lock_irqsave(&g_tempd.lock);
  active = g_tempd.active;
  g_tempd.active = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  if (active)
    {
      work_cancel_sync(LPWORK, &g_tempd.work);
      manual_cal_temp_pwr_unint();
    }

  return BK_OK;
}

bool temp_detect_is_init(void)
{
  irqstate_t flags;
  bool active;

  flags = spin_lock_irqsave(&g_tempd.lock);
  active = g_tempd.active;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active;
}

static void bk7258_tempd_worker(void *arg)
{
  struct bk7258_tempd_state *state = arg;
  uint32_t temperature;
  uint32_t delay_ms;
  irqstate_t flags;
  bool active;
  int ret;

  flags = spin_lock_irqsave(&state->lock);
  active = state->active;
  spin_unlock_irqrestore(&state->lock, flags);
  if (!active)
    {
      return;
    }

  ret = temp_detect_get_temperature(&temperature);
  if (ret == BK_OK)
    {
      /* The pinned libbk_phy.a provider is declared in temp_detect_pub.h.
       * Keep this in worker context: the vendor implementation retunes PHY
       * calibration state and is not an interrupt-safe notification hook. */

      rwnx_cal_do_temp_detect((uint16_t)temperature,
                              ADC_TMEP_LSB_PER_10DEGREE *
                              ADC_TMEP_10DEGREE_PER_DBPWR,
                              &state->last_temperature);
    }
  else
    {
      BK_LOGW(ANALOG_TAG, "temperature sample unavailable=%d\n", ret);
    }

  flags = spin_lock_irqsave(&state->lock);
  if (state->active)
    {
      state->sample_count++;
      delay_ms = state->sample_count >=
                 ADC_TMEP_DETECT_INTERVAL_CHANGE /
                 ADC_TMEP_DETECT_INTERVAL_INIT ?
                 ADC_TMEP_DETECT_INTERVAL * 1000U :
                 ADC_TMEP_DETECT_INTERVAL_INIT * 1000U;
      /* Schedule while holding the lifecycle lock. If deinit wins first it
       * clears active and cancels this work; if this wins first, deinit sees
       * the pending work and work_cancel_sync() waits for/cancels it. */

      ret = work_queue(LPWORK, &state->work, bk7258_tempd_worker, state,
                       MSEC2TICK(delay_ms));
      if (ret < 0)
        {
          state->active = false;
          BK_LOGE(ANALOG_TAG, "temperature reschedule failed=%d\n", ret);
        }
    }
  spin_unlock_irqrestore(&state->lock, flags);
}

int temp_detect_get_temperature(uint32_t *temperature)
{
  uint16_t samples[ADC_TEMP_BUFFER_SIZE] = {0};
  adc_config_t config = {0};
  uint32_t sum = 0;
  uint32_t count = 0;
  uint32_t result = 0;
  bool acquired = false;
  bool initialized = false;
  bool temp_enabled = false;
  bk_err_t ret;
  unsigned int index;

  if (temperature == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = bk_adc_acquire();
  if (ret != BK_OK)
    {
      return ret;
    }
  acquired = true;

  ret = bk7258_analog_update_bits(5, UINT32_C(1) << 4, UINT32_C(1) << 4);
  if (ret < 0)
    {
      ret = BK_FAIL;
      goto cleanup;
    }
  temp_enabled = true;

  ret = bk_adc_init(ADC_TEMP_SENSOR_CHANNEL);
  if (ret != BK_OK)
    {
      goto cleanup;
    }
  initialized = true;

  config.chan = ADC_TEMP_SENSOR_CHANNEL;
  config.adc_mode = ADC_CONTINUOUS_MODE;
  config.clk = TEMP_DETEC_ADC_CLK;
  config.src_clk = ADC_SCLK_XTAL_26M;
  config.saturate_mode = ADC_TEMP_SATURATE_MODE;
  config.sample_rate = 0;
  config.steady_ctrl = TEMP_DETEC_ADC_STEADY_CTRL;
  config.adc_filter = 0;

  ret = bk_adc_set_phy_cali_config(&config);
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_enable_bypass_clalibration();
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_start();
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  ret = bk_adc_read_raw(samples, ADC_TEMP_BUFFER_SIZE, 1000);
  if (ret != BK_OK)
    {
      ret = BK_ERR_TEMPD_BASE - 1;
      goto cleanup;
    }

  for (index = 5; index < ADC_TEMP_BUFFER_SIZE; index++)
    {
      if (samples[index] != 0 && samples[index] != 2048)
        {
          sum += samples[index];
          count++;
        }
    }

  if (count != 0)
    {
      result = (sum / count) / 4;
    }

  if (result <= ADC_TEMP_VAL_MIN || result >= ADC_TEMP_VAL_MAX)
    {
      ret = BK_ERR_TRY_AGAIN;
    }
  else
    {
      ret = BK_OK;
    }

cleanup:
  if (temp_enabled && bk7258_analog_update_bits(5, UINT32_C(1) << 4, 0) < 0 &&
      ret == BK_OK)
    {
      ret = BK_FAIL;
    }

  if (initialized)
    {
      (void)bk_adc_stop();
      (void)bk_adc_deinit(ADC_TEMP_SENSOR_CHANNEL);
    }

  if (acquired)
    {
      (void)bk_adc_release();
    }

  /* This is the vendor PHY's processed temperature-sensor ADC code, not °C. */
  *temperature = result;
  return ret;
}

int volt_single_get_current_voltage(UINT32 *volt_value)
{
  (void)volt_value;
  ANALOG_UNPORTED("volt_single_get_current_voltage");
  return -1;
}
