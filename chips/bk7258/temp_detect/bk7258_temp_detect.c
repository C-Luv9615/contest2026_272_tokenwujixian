/****************************************************************************
 * chips/bk7258/temp_detect/bk7258_temp_detect.c
 *
 * BK7258 on-chip temperature sampling and periodic PHY compensation.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <common/bk_err.h>
#include <common/bk_typedef.h>
#include <components/log.h>
#include <components/sensor.h>
#include <modules/pm.h>
#include <arm_internal.h>
#include <bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include <math.h>

#include <driver/adc.h>
#include <os/os.h>
#include <bk_private/bk_phy.h>
#include <temp_detect.h>
#include <temp_detect_pub.h>
#include <volt_detect.h>

#define TEMPD_TAG "BK7258 TEMPD"

struct bk7258_tempd_state
{
  struct work_s work;
  spinlock_t lock;
  bool active;
  bool initialised;
  bool sensor_ready;
  bool temperature_valid;
  bool voltage_enabled;
  bool voltage_valid;
  uint16_t last_temperature;
  float temperature;
  float voltage;
  uint32_t sample_count;
  uint16_t detect_interval;
  uint16_t detect_threshold;
  uint16_t dist_inital;
};

static struct bk7258_tempd_state g_tempd =
{
  .lock = SP_UNLOCKED,
};

static void bk7258_tempd_worker(void *arg);

/* Authority BK7236-family voltage detection samples ADC0. */
#define BK7258_VOLT_SENSOR_CHANNEL ADC_0

static int bk7258_tempd_sample(uint32_t *temperature)
{
  uint16_t samples[ADC_TEMP_BUFFER_SIZE] = {0};
  adc_config_t config = {0};
  uint32_t sum = 0;
  uint32_t count = 0;
  bool acquired = false;
  bool initialized = false;
  bool temp_enabled = false;
  bk_err_t ret;
  unsigned int index;

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
  config.sample_rate = TEMP_DETEC_ADC_SAMPLE_RATE;
  config.steady_ctrl = TEMP_DETEC_ADC_STEADY_CTRL;

  /* Authority temp_detect.c uses the regular configuration path so channel 7
   * receives ANA5's normal 1/3 divider setup. PHY calibration intentionally
   * uses the distinct no-divider path. */
  ret = bk_adc_set_config(&config);
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
  *temperature = count == 0 ? 0 : (sum / count) / 4;
  ret = BK_OK;

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
  return ret;
}

static int bk7258_voltd_sample(uint32_t *voltage)
{
  uint16_t samples[ADC_TEMP_BUFFER_SIZE] = {0};
  adc_config_t config = {0};
  uint32_t sum = 0;
  uint32_t count = 0;
  bool acquired = false;
  bool initialized = false;
  bk_err_t ret;
  unsigned int index;

  if (voltage == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = bk_adc_acquire();
  if (ret != BK_OK)
    {
      return ret;
    }

  acquired = true;
  ret = bk_adc_init(BK7258_VOLT_SENSOR_CHANNEL);
  if (ret != BK_OK)
    {
      goto cleanup;
    }

  initialized = true;
  config.chan = BK7258_VOLT_SENSOR_CHANNEL;
  config.adc_mode = ADC_CONTINUOUS_MODE;
  config.clk = TEMP_DETEC_ADC_CLK;
  config.src_clk = ADC_SCLK_XTAL_26M;
  config.saturate_mode = ADC_TEMP_SATURATE_MODE;
  config.sample_rate = TEMP_DETEC_ADC_SAMPLE_RATE;
  config.steady_ctrl = TEMP_DETEC_ADC_STEADY_CTRL;
  config.adc_filter = 0;

  ret = bk_adc_set_config(&config);
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

  if (count == 0)
    {
      ret = BK_ERR_TRY_AGAIN;
      goto cleanup;
    }

  *voltage = (sum / count) / 4;
  ret = *voltage > ADC_TEMP_VAL_MIN ? BK_OK : BK_ERR_TRY_AGAIN;

cleanup:
  if (initialized)
    {
      (void)bk_adc_stop();
      (void)bk_adc_deinit(BK7258_VOLT_SENSOR_CHANNEL);
    }

  if (acquired)
    {
      (void)bk_adc_release();
    }

  return ret;
}

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
  g_tempd.initialised = true;
  g_tempd.sensor_ready = true;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_enabled = false;
  g_tempd.voltage_valid = false;
  g_tempd.last_temperature = (uint16_t)init_val;
  g_tempd.sample_count = 0;
  g_tempd.detect_interval = ADC_TMEP_DETECT_INTERVAL;
  g_tempd.detect_threshold = ADC_TMEP_LSB_PER_10DEGREE *
                             ADC_TMEP_10DEGREE_PER_DBPWR;
  g_tempd.dist_inital = ADC_TMEP_DIST_INTIAL_VAL;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  /* The sensor value store must exist before the worker publishes into it. */

  ret = (int)bk_sensor_init();
  volt_daemon_init();

  flags = spin_lock_irqsave(&g_tempd.lock);
  ret = work_queue(LPWORK, &g_tempd.work, bk7258_tempd_worker, &g_tempd, 0);
  if (ret < 0)
    {
      g_tempd.active = false;
      g_tempd.sensor_ready = false;
      spin_unlock_irqrestore(&g_tempd.lock, flags);
      BK_LOGE(TEMPD_TAG, "schedule failed=%d\n", ret);
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
  g_tempd.initialised = false;
  g_tempd.sensor_ready = false;
  g_tempd.temperature_valid = false;
  g_tempd.voltage_valid = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  if (active)
    {
      work_cancel_sync(LPWORK, &g_tempd.work);
      manual_cal_temp_pwr_unint();
    }

  volt_daemon_deinit();
  (void)bk_sensor_deinit();

  return BK_OK;
}

bool temp_detect_is_init(void)
{
  irqstate_t flags;
  bool inited;

  /* The authority reports task+queue existence here, not "the timer is
   * running": temp_detect_stop() pauses sampling but leaves the daemon
   * established, so is_init() stays true.  Keep that split.
   */

  flags = spin_lock_irqsave(&g_tempd.lock);
  inited = g_tempd.initialised;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return inited;
}

static void bk7258_tempd_worker(void *arg)
{
  struct bk7258_tempd_state *state = arg;
  uint32_t temperature;
  uint32_t voltage;
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
      rwnx_cal_do_temp_detect((uint16_t)temperature,
                              ADC_TMEP_LSB_PER_10DEGREE *
                              ADC_TMEP_10DEGREE_PER_DBPWR,
                              &state->last_temperature);
      /* No bk_sensor_set_current_temperature() here: nothing in the baseline
       * ever publishes into g_sensor_info (grep finds only the definition),
       * so its consumers see BK_ERR_TRY_AGAIN and handle it.  Publishing a
       * raw ADC count would change both the availability contract and the
       * unit that pm_misc.c prints as "temperature about %.2f".
       */
    }
  else
    {
      BK_LOGW(TEMPD_TAG, "temperature sample unavailable=%d\n", ret);
    }

  flags = spin_lock_irqsave(&state->lock);
  active = state->active && state->voltage_enabled;
  spin_unlock_irqrestore(&state->lock, flags);
  if (active)
    {
      /* Same entry point the authority reaches on VOLT_TIMER_EXPIRED: it
       * owns the SARADC sleep vote, the sample, and the PHY notification.
       */
      volt_daemon_polling_handler();
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
      ret = work_queue(LPWORK, &state->work, bk7258_tempd_worker, state,
                       MSEC2TICK(delay_ms));
      if (ret < 0)
        {
          state->active = false;
          BK_LOGE(TEMPD_TAG, "reschedule failed=%d\n", ret);
        }
    }
  spin_unlock_irqrestore(&state->lock, flags);
}

int temp_detect_get_temperature(uint32_t *temperature)
{
  uint32_t result = 0;
  bk_err_t ret = BK_ERR_TRY_AGAIN;
  unsigned int attempt;

  if (temperature == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  /* Authority retries the complete transaction up to TEMPD_MAX_RETRY_NUM
   * times if sampling fails or the processed code is out of sensor range. */
  for (attempt = 0; attempt < 3; attempt++)
    {
      ret = bk7258_tempd_sample(&result);
      if (ret == BK_OK && result > ADC_TEMP_VAL_MIN &&
          result < ADC_TEMP_VAL_MAX)
        {
          break;
        }
      if (ret == BK_OK)
        {
          ret = BK_ERR_TRY_AGAIN;
        }
    }
  *temperature = result;
  return ret;
}

int volt_single_get_current_voltage(UINT32 *volt_value)
{
  uint32_t voltage;
  bk_err_t ret;

  if (volt_value == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = bk7258_voltd_sample(&voltage);
  if (ret == BK_OK)
    {
      *volt_value = (UINT32)voltage;
    }

  return ret;
}

int volt_detect_start(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);
  bool active = g_tempd.active;

  g_tempd.voltage_enabled = true;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active ? BK_OK : BK_ERR_NOT_INIT;
}

int volt_detect_stop(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);
  bool active = g_tempd.active;

  g_tempd.voltage_enabled = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return active ? BK_OK : BK_ERR_NOT_INIT;
}

/*===========================================================================
 * bk_sensor.c face
 *
 * The value store and its locking are the authority's: a beken mutex, NaN as
 * "no sample yet", BK_ERR_TRY_AGAIN while it is still NaN.  Two units read
 * these values today (pm_misc.c and bk_phy_server.c), so the error codes are
 * part of the contract, not an implementation detail.
 *
 * bk_sensor_load_adc_cali_value() is compiled in because CONFIG_OTP_V1 is set
 * for this build: it loads the GADC/SDMADC calibration pairs from OTP and
 * programs the SARADC/SDMADC calibrate registers.  Without it every
 * temperature and voltage reading would be uncalibrated -- which is exactly
 * what the boot log's "uncali sdmadc value" warning reports when the OTP
 * fields are blank, as they are on this board.
 *=========================================================================*/

#if (CONFIG_TEMP_DETECT || CONFIG_VOLT_DETECT)
typedef struct
{
  beken_mutex_t lock;
  float         temperature;
  float         voltage;
} MCU_SENSOR_INFO;

MCU_SENSOR_INFO g_sensor_info;
#endif

/* The OTP calibration load lives in bk7258_sensor_cal.c: it needs the
 * baseline's bk_saradc.h, and that header and this file's hal_port ADC
 * headers redefine hal_adc_types.h.  One TU each keeps both families intact.
 */

extern bk_err_t bk7258_sensor_load_adc_cali_value(void);

bk_err_t bk_sensor_init(void)
{
  bk_err_t ret = BK_OK;

  (void)bk7258_sensor_load_adc_cali_value();
#if (CONFIG_TEMP_DETECT || CONFIG_VOLT_DETECT)
  if (NULL == g_sensor_info.lock)
    {
      ret = rtos_init_mutex(&g_sensor_info.lock);
      g_sensor_info.voltage = NAN;
      g_sensor_info.temperature = NAN;
    }

#if CONFIG_PHY_MB
  bk_err_t ret_mb = BK_OK;
  extern bk_err_t mb_phy_ipc_init(void);
  ret_mb = mb_phy_ipc_init();
  if (ret_mb != BK_OK)
    {
      BK_LOGE("phy_driver", "mb_phy_ipc_init failed %d.\r\n", ret);
    }
  else
    {
#if (CONFIG_CPU_CNT > 1)
      extern bk_err_t bk_phy_server_init(void);
      ret_mb = bk_phy_server_init();
      if (ret_mb != BK_OK)
        {
          BK_LOGE("phy_driver", "phy svr create failed %d.\r\n", ret);
        }
#endif /* CONFIG_CPU_CNT */
    }
#endif /* CONFIG_PHY_MB */
#endif /* CONFIG_TEMP_DETECT || CONFIG_VOLT_DETECT */
  return ret;
}

bk_err_t bk_sensor_deinit(void)
{
  bk_err_t ret = BK_OK;

#if (CONFIG_TEMP_DETECT || CONFIG_VOLT_DETECT)
  if (NULL != g_sensor_info.lock)
    {
      ret = rtos_deinit_mutex(&g_sensor_info.lock);
      g_sensor_info.lock = NULL;
      g_sensor_info.voltage = NAN;
      g_sensor_info.temperature = NAN;
    }
#endif
  return ret;
}

#if (CONFIG_TEMP_DETECT)
bk_err_t bk_sensor_set_current_temperature(float temperature)
{
  if (NULL == g_sensor_info.lock)
    {
      return BK_ERR_NOT_INIT;
    }

  rtos_lock_mutex(&g_sensor_info.lock);
  g_sensor_info.temperature = temperature;
  rtos_unlock_mutex(&g_sensor_info.lock);

  return BK_OK;
}

bk_err_t bk_sensor_get_current_temperature(float *temperature)
{
  if (NULL == g_sensor_info.lock)
    {
      return BK_ERR_NOT_INIT;
    }

  if (isnan(g_sensor_info.temperature))
    {
      return BK_ERR_TRY_AGAIN;
    }

  if (NULL == temperature)
    {
      return BK_ERR_PARAM;
    }

  rtos_lock_mutex(&g_sensor_info.lock);
  *temperature = g_sensor_info.temperature;
  rtos_unlock_mutex(&g_sensor_info.lock);

  return BK_OK;
}
#endif /* CONFIG_TEMP_DETECT */

#if (CONFIG_VOLT_DETECT)
bk_err_t bk_sensor_set_current_voltage(float voltage)
{
  if (NULL == g_sensor_info.lock)
    {
      return BK_ERR_NOT_INIT;
    }

  rtos_lock_mutex(&g_sensor_info.lock);
  g_sensor_info.voltage = voltage;
  rtos_unlock_mutex(&g_sensor_info.lock);

  return BK_OK;
}

bk_err_t bk_sensor_get_current_voltage(float *voltage)
{
  if (NULL == g_sensor_info.lock)
    {
      return BK_ERR_NOT_INIT;
    }

  if (isnan(g_sensor_info.voltage))
    {
      return BK_ERR_TRY_AGAIN;
    }

  if (NULL == voltage)
    {
      return BK_ERR_PARAM;
    }

  rtos_lock_mutex(&g_sensor_info.lock);
  *voltage = g_sensor_info.voltage;
  rtos_unlock_mutex(&g_sensor_info.lock);

  return BK_OK;
}
#endif /* CONFIG_VOLT_DETECT */

/*===========================================================================
 * Exported data symbols
 *
 * Both are never read inside the baseline (the CLI walks the descriptor
 * tables instead), but they are part of its export set, so they exist here
 * with the authority types.  saradc_desc_t comes from bk_private/bk_saradc.h
 * and temp_detect_config_t from temp_detect.h.
 *=========================================================================*/

temp_detect_config_t g_temp_detect_config;

/* saradc_desc_t tmp_detect_desc lives in bk7258_sensor_cal.c: its type comes
 * from the baseline bk_saradc.h, which this file cannot include (see the note
 * at the top of that file).
 */

/* volt_detect.c keeps its config type in the .c file; same layout here. */

typedef struct
{
  uint16_t last_detect_val;
  uint16_t detect_interval;
  uint16_t detect_threshold;
  uint16_t inital_data;
  uint32_t detect_cnt;
} bk7258_volt_detect_config_t;

bk7258_volt_detect_config_t s_voltd;

/*===========================================================================
 * temp_detect.c control face
 *
 * The authority drives these through a message queue consumed by its daemon
 * task.  This port has no queue: the daemon is a self-rescheduling LPWORK
 * worker, so a message is executed where it is posted.  What stays identical
 * is the observable part -- the return codes (0 accepted, BK_FAIL when the
 * daemon is not established), the "pause only stops the timer, the daemon
 * stays established" split, and the defaulting rules of
 * temp_detect_change_configuration().
 *=========================================================================*/

int temp_detect_send_msg(uint32_t msg_type)
{
  if (!temp_detect_is_init())
    {
      return BK_FAIL;
    }

  switch (msg_type)
    {
      case TMPD_PAUSE_TIMER:
        return temp_detect_stop();

      case TMPD_RESTART_TIMER:
        return temp_detect_start();

      case TMPD_CHANGE_PARAM:
        /* Callers apply the new parameters directly; nothing to post. */

        return BK_OK;

      case TMPD_TIMER_EXPIRED:
        {
          irqstate_t flags = spin_lock_irqsave(&g_tempd.lock);
          bool active = g_tempd.active;
          spin_unlock_irqrestore(&g_tempd.lock, flags);

          if (active && work_available(&g_tempd.work))
            {
              (void)work_queue(LPWORK, &g_tempd.work, bk7258_tempd_worker,
                               &g_tempd, 0);
            }

          return BK_OK;
        }

      case VOLT_PAUSE_TIMER:
        return volt_daemon_stop();

      case VOLT_RESTART_TIMER:
        return volt_daemon_restart();

      case VOLT_TIMER_EXPIRED:
        volt_daemon_polling_handler();
        return BK_OK;

      case TMPD_DEINIT:
        return temp_detect_deinit();

      default:
        return BK_FAIL;
    }
}

int temp_detect_start(void)
{
  irqstate_t flags;
  int ret = BK_OK;

  flags = spin_lock_irqsave(&g_tempd.lock);
  if (!g_tempd.active)
    {
      g_tempd.active = true;
      if (work_available(&g_tempd.work))
        {
          ret = work_queue(LPWORK, &g_tempd.work, bk7258_tempd_worker,
                           &g_tempd, 0);
          if (ret < 0)
            {
              g_tempd.active = false;
            }
        }
    }

  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return ret < 0 ? BK_FAIL : BK_OK;
}

int temp_detect_stop(void)
{
  irqstate_t flags;

  /* Pause, do not tear down: the authority stops the detect timer and leaves
   * the daemon task and its queue alive, so is_init() keeps returning true.
   * The worker checks active before it reschedules, so clearing the flag is
   * enough -- and it stays safe when called from the worker itself.
   */

  flags = spin_lock_irqsave(&g_tempd.lock);
  g_tempd.active = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

void temp_detect_change_configuration(uint32_t interval, uint32_t threshold,
                                      uint32_t dist)
{
  irqstate_t flags;
  bool interval_changed;

  if (interval == 0)
    {
      interval = ADC_TMEP_DETECT_INTERVAL;
    }

  if (threshold == 0)
    {
      threshold = ADC_TMEP_LSB_PER_10DEGREE * ADC_TMEP_10DEGREE_PER_DBPWR;
    }

  if (dist == 0)
    {
      dist = ADC_TMEP_DIST_INTIAL_VAL;
    }

  BK_LOGD(TEMPD_TAG, "change config, interval=%d, threshold=%d, dist=%d\n",
          interval, threshold, dist);

  flags = spin_lock_irqsave(&g_tempd.lock);
  interval_changed = (g_tempd.detect_interval != (uint16_t)interval) ||
                     (g_tempd.detect_threshold != (uint16_t)threshold) ||
                     (g_tempd.dist_inital != (uint16_t)dist);
  g_tempd.detect_interval = (uint16_t)interval;
  g_tempd.detect_threshold = (uint16_t)threshold;
  g_tempd.dist_inital = (uint16_t)dist;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  if (interval_changed)
    {
      /* Same ordering as the authority: re-arm the compensation window first,
       * then let the running worker pick up the new interval on its next
       * pass (the worker reschedules itself, so no timer to re-init here).
       */

      manual_cal_tmp_pwr_init((uint16_t)g_tempd.last_temperature,
                              (uint16_t)threshold, (uint16_t)dist);
      (void)temp_detect_start();
    }
}

/*===========================================================================
 * volt_detect.c daemon face
 *
 * The timer becomes the worker's voltage branch: init/restart ensure the
 * worker is scheduled, stop disables the branch, polling_handler performs one
 * sample.  The SARADC sleep-vote ordering (release -> sample -> notify ->
 * re-vote) is the authority's and is kept inside the worker.
 *=========================================================================*/

int volt_daemon_restart(void)
{
  return temp_detect_send_msg(VOLT_RESTART_TIMER) == BK_OK ? BK_OK : BK_FAIL;
}

int volt_daemon_stop(void)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_tempd.lock);
  g_tempd.voltage_enabled = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
  return BK_OK;
}

void volt_daemon_polling_handler(void)
{
  uint32_t voltage;
  int result;

  (void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 0, 0);
  result = bk7258_voltd_sample(&voltage);
  if (result != BK_OK)
    {
      BK_LOGW(TEMPD_TAG, "detect failed(%d), retry\n", result);
      (void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1, 0);
      return;
    }

  rwnx_cal_do_volt_detect((uint16_t)voltage);

  /* Same reasoning as the temperature branch: the baseline's value store is
   * never written, so leave it at NaN and let getters report TRY_AGAIN.
   */
  (void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1, 0);
}

void volt_daemon_init(void)
{
  irqstate_t flags;

  /* Interval choice is the authority's: 1 s while ATE runs, 15 s otherwise.
   * The worker owns the 1 s -> steady-state switch, so only the enable flag
   * and the counters are reset here.
   */

  flags = spin_lock_irqsave(&g_tempd.lock);
  g_tempd.voltage_enabled = true;
  g_tempd.voltage_valid = false;
  g_tempd.sample_count = 0;
  spin_unlock_irqrestore(&g_tempd.lock, flags);

  s_voltd.last_detect_val = 0;
  s_voltd.inital_data = 0;
  s_voltd.detect_threshold = 0;
  s_voltd.detect_interval = ate_is_enabled() ? ADC_TMEP_DETECT_INTERVAL_INIT
                                             : ADC_TMEP_DETECT_INTERVAL;
  s_voltd.detect_cnt = 0;

  (void)temp_detect_start();
}

void volt_daemon_deinit(void)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_tempd.lock);
  g_tempd.voltage_enabled = false;
  g_tempd.voltage_valid = false;
  spin_unlock_irqrestore(&g_tempd.lock, flags);
}
