/****************************************************************************
 * chips/bk7258/wifi/glue/bk7258_saradc.c
 *
 * Minimal BK7258 SARADC adapter for the Armino PHY calibration callbacks.
 * Register layout and configuration semantics are from the BK7258 Armino
 * adc_ll.h, adc_struct.h, adc_hal.c, and adc_driver.c.
 ****************************************************************************/

#if 0 /* Moved to chips/bk7258/saradc/bk7258_saradc.c. */

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>
#include <syslog.h>

#include <arm_internal.h>

#include <bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include <common/bk_err.h>
#include <driver/adc.h>

#define SADC_TAG "BK7258 SARADC"

/* adc_struct.h register offsets. */

#define SADC_GLOBAL_CTRL       (BK7258_SADC_BASE + UINT32_C(0x08))
#define SADC_CTRL              (BK7258_SADC_BASE + UINT32_C(0x10))
#define SADC_STEADY_CTRL       (BK7258_SADC_BASE + UINT32_C(0x18))
#define SADC_SAT_CTRL          (BK7258_SADC_BASE + UINT32_C(0x1c))
#define SADC_DATA              (BK7258_SADC_BASE + UINT32_C(0x20))

#define SADC_GLOBAL_SOFT_RESET (UINT32_C(1) << 0)
#define SADC_CTRL_MODE_MASK    UINT32_C(0x3)
#define SADC_CTRL_ENABLE       (UINT32_C(1) << 2)
#define SADC_CTRL_CHANNEL_MASK (UINT32_C(0xf) << 3)
#define SADC_CTRL_SETTING_8    (UINT32_C(1) << 7)
#define SADC_CTRL_INT_CLEAR    (UINT32_C(1) << 8)
#define SADC_CTRL_DIV_MASK     (UINT32_C(0x3f) << 9)
#define SADC_CTRL_32M          (UINT32_C(1) << 15)
#define SADC_CTRL_RATE_MASK    (UINT32_C(0x3f) << 16)
#define SADC_CTRL_FILTER_MASK  (UINT32_C(0x7f) << 22)
#define SADC_CTRL_BUSY         (UINT32_C(1) << 29)
#define SADC_CTRL_FIFO_EMPTY   (UINT32_C(1) << 30)

#define SADC_STEADY_FIFO_MASK  UINT32_C(0x1f)
#define SADC_STEADY_TIME_MASK  (UINT32_C(0x7) << 5)
#define SADC_STEADY_BYPASS     (UINT32_C(1) << 10)
#define SADC_SAT_MODE_MASK     UINT32_C(0x3)
#define SADC_SAT_ENABLE        (UINT32_C(1) << 2)

#define SADC_ANALOG_REG        2
#define SADC_ANALOG_CMP_MASK   (UINT32_C(0x3) << 9)
#define SADC_ANALOG_INBUF_MASK (UINT32_C(0x3) << 11)
#define SADC_ANALOG_REFBUF_MASK (UINT32_C(0x3) << 13)
#define SADC_ANALOG_NOBUF      (UINT32_C(1) << 15)
#define SADC_ANALOG_CAPCAL_MASK (UINT32_C(0x3f) << 19)
#define SADC_ANALOG_SP_NT_MASK (UINT32_C(0x7f) << 25)

#define SADC_SRC_DCO_HZ        UINT32_C(120000000)
#define SADC_SRC_XTAL_HZ       UINT32_C(26000000)
#define SADC_SRC_DPLL_HZ       UINT32_C(240000000)
#define SADC_SRC_32M_HZ        UINT32_C(32000000)

struct bk7258_sadc_state
{
  mutex_t lock;
  bool lock_ready;
  bool initialized;
  bool configured;
  bool started;
  bool acquired;
  adc_chan_t channel;
  adc_config_t config;
};

static struct bk7258_sadc_state g_sadc;
static spinlock_t g_sadc_init_lock = SP_UNLOCKED;

static bk_err_t bk7258_sadc_error(int error)
{
  return error == OK ? BK_OK : BK_FAIL;
}

static bool bk7258_sadc_valid_channel(adc_chan_t channel)
{
  return (unsigned int)channel < ADC_MAX;
}

static int bk7258_sadc_prepare_lock(void)
{
  irqstate_t flags;
  int ret = OK;

  flags = spin_lock_irqsave(&g_sadc_init_lock);
  if (!g_sadc.lock_ready)
    {
      ret = nxmutex_init(&g_sadc.lock);
      if (ret == OK)
        {
          g_sadc.lock_ready = true;
        }
    }
  spin_unlock_irqrestore(&g_sadc_init_lock, flags);
  return ret;
}

static void bk7258_sadc_set_field(uintptr_t reg, uint32_t mask,
                                  unsigned int shift, uint32_t value)
{
  modifyreg32(reg, mask, (value << shift) & mask);
}

static int bk7258_sadc_analog_setup(bool enable)
{
  uint32_t value;
  int ret;

  ret = bk7258_analog_read(SADC_ANALOG_REG, &value);
  if (ret < 0)
    {
      return ret;
    }

  if (enable)
    {
      value &= ~(SADC_ANALOG_CMP_MASK | SADC_ANALOG_INBUF_MASK |
                 SADC_ANALOG_REFBUF_MASK | SADC_ANALOG_CAPCAL_MASK |
                 SADC_ANALOG_SP_NT_MASK);
      value |= (UINT32_C(2) << 9) | (UINT32_C(2) << 11) |
               (UINT32_C(2) << 13) | SADC_ANALOG_NOBUF |
               (UINT32_C(3) << 25);
    }
  else
    {
      value &= ~SADC_ANALOG_NOBUF;
    }

  return bk7258_analog_write(SADC_ANALOG_REG, value);
}

static int bk7258_sadc_source_hz(adc_src_clk_t source, uint32_t *hz)
{
  if (hz == NULL)
    {
      return -EINVAL;
    }

  switch (source)
    {
      case ADC_SCLK_DCO:
        *hz = SADC_SRC_DCO_HZ;
        modifyreg32(BK7258_SYS_CLKDIV1, 0, BK7258_SYS_SADC_CLK_DCO);
        return OK;

      case ADC_SCLK_XTAL_26M:
        *hz = SADC_SRC_XTAL_HZ;
        modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_SADC_CLK_DCO, 0);
        return OK;

      case ADC_SCLK_DPLL:
        /* The verified BK7258 HAL selects the DCO path for DPLL too; its
         * audio-PLL enable macro is intentionally a no-op on this SoC. */
        *hz = SADC_SRC_DPLL_HZ;
        modifyreg32(BK7258_SYS_CLKDIV1, 0, BK7258_SYS_SADC_CLK_DCO);
        return OK;

      case ADC_SCLK_32M:
        *hz = SADC_SRC_32M_HZ;
        return OK;

      default:
        return -EINVAL;
    }
}

static int bk7258_sadc_apply_config(const adc_config_t *config)
{
  uint32_t source_hz;
  uint32_t divider;
  int ret;

  if (config == NULL || !bk7258_sadc_valid_channel(config->chan) ||
      config->adc_mode >= ADC_NONE_MODE || config->src_clk >= ADC_SCLK_NONE ||
      config->clk == 0)
    {
      return -EINVAL;
    }

  ret = bk7258_sadc_source_hz(config->src_clk, &source_hz);
  if (ret < 0 || source_hz / 2 < config->clk)
    {
      return -EINVAL;
    }

  divider = source_hz / (UINT32_C(2) * config->clk);
  if (divider == 0 || divider > 64)
    {
      return -ERANGE;
    }

  divider--;
  bk7258_sadc_set_field(SADC_CTRL, SADC_CTRL_CHANNEL_MASK, 3, config->chan);
  bk7258_sadc_set_field(SADC_CTRL, SADC_CTRL_MODE_MASK, 0, config->adc_mode);
  bk7258_sadc_set_field(SADC_CTRL, SADC_CTRL_DIV_MASK, 9, divider);
  bk7258_sadc_set_field(SADC_CTRL, SADC_CTRL_RATE_MASK, 16,
                        config->sample_rate);
  bk7258_sadc_set_field(SADC_CTRL, SADC_CTRL_FILTER_MASK, 22,
                        config->adc_filter);
  bk7258_sadc_set_field(SADC_STEADY_CTRL, SADC_STEADY_TIME_MASK, 5,
                        config->steady_ctrl);

  if (config->src_clk == ADC_SCLK_32M)
    {
      modifyreg32(SADC_CTRL, 0, SADC_CTRL_32M);
    }
  else
    {
      modifyreg32(SADC_CTRL, SADC_CTRL_32M, 0);
    }

  if (config->saturate_mode == ADC_SATURATE_MODE_NONE)
    {
      modifyreg32(SADC_SAT_CTRL, SADC_SAT_ENABLE, 0);
    }
  else
    {
      bk7258_sadc_set_field(SADC_SAT_CTRL, SADC_SAT_MODE_MASK, 0,
                            config->saturate_mode);
      modifyreg32(SADC_SAT_CTRL, 0, SADC_SAT_ENABLE);
    }

  g_sadc.config = *config;
  g_sadc.configured = true;
  return OK;
}

bk_err_t bk_adc_acquire(void)
{
  int ret = bk7258_sadc_prepare_lock();

  if (ret < 0)
    {
      return bk7258_sadc_error(ret);
    }

  ret = nxmutex_lock(&g_sadc.lock);
  if (ret == OK)
    {
      g_sadc.acquired = true;
    }

  return bk7258_sadc_error(ret);
}

bk_err_t bk_adc_release(void)
{
  int ret;

  if (!g_sadc.lock_ready || !g_sadc.acquired)
    {
      return BK_FAIL;
    }

  g_sadc.acquired = false;
  ret = nxmutex_unlock(&g_sadc.lock);
  return bk7258_sadc_error(ret);
}

bk_err_t bk_adc_init(adc_chan_t channel)
{
  int ret;

  if (!g_sadc.acquired)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  if (!bk7258_sadc_valid_channel(channel))
    {
      return BK_ERR_ADC_INVALID_CHAN;
    }

  modifyreg32(BK7258_SYS_DEV_CLK_EN, 0, BK7258_SYS_SADC_CLK_EN);
  ret = bk7258_sadc_analog_setup(true);
  if (ret < 0)
    {
      modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_SADC_CLK_EN, 0);
      syslog(LOG_ERR, "[%s] analog setup failed: %d\n", SADC_TAG, ret);
      return BK_FAIL;
    }

  putreg32(SADC_GLOBAL_SOFT_RESET, SADC_GLOBAL_CTRL);
  putreg32(0, SADC_CTRL);
  putreg32(0, SADC_STEADY_CTRL);
  putreg32(0, SADC_SAT_CTRL);
  modifyreg32(SADC_CTRL, SADC_CTRL_SETTING_8, 0);
  bk7258_sadc_set_field(SADC_STEADY_CTRL, SADC_STEADY_FIFO_MASK, 0, 31);

  g_sadc.channel = channel;
  g_sadc.initialized = true;
  g_sadc.configured = false;
  g_sadc.started = false;
  return BK_OK;
}

bk_err_t bk_adc_deinit(adc_chan_t channel)
{
  int ret;

  if (!g_sadc.acquired || !g_sadc.initialized)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  if (!bk7258_sadc_valid_channel(channel) || channel != g_sadc.channel)
    {
      return BK_ERR_ADC_INVALID_CHAN;
    }

  modifyreg32(SADC_CTRL, SADC_CTRL_ENABLE, 0);
  putreg32(0, SADC_CTRL);
  putreg32(0, SADC_STEADY_CTRL);
  putreg32(0, SADC_SAT_CTRL);
  ret = bk7258_sadc_analog_setup(false);
  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_SADC_CLK_EN, 0);

  g_sadc.initialized = false;
  g_sadc.configured = false;
  g_sadc.started = false;
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] analog teardown failed channel=%u ret=%d\n",
             SADC_TAG, (unsigned int)channel, ret);
    }

  return bk7258_sadc_error(ret);
}

bk_err_t bk_adc_set_phy_cali_config(adc_config_t *config)
{
  int ret;

  if (!g_sadc.acquired || !g_sadc.initialized)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  if (config == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  if (!bk7258_sadc_valid_channel(config->chan))
    {
      return BK_ERR_ADC_INVALID_CHAN;
    }

  if (config->adc_mode >= ADC_NONE_MODE)
    {
      return BK_ERR_ADC_INVALID_MODE;
    }

  if (config->src_clk >= ADC_SCLK_NONE)
    {
      return BK_ERR_ADC_INVALID_SCLK_MODE;
    }

  ret = bk7258_sadc_apply_config(config);
  if (ret == -EINVAL)
    {
      return BK_ERR_PARAM;
    }
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] rejected PHY config: %d\n", SADC_TAG, ret);
      return BK_FAIL;
    }

  return BK_OK;
}

bk_err_t bk_adc_enable_bypass_clalibration(void)
{
  if (!g_sadc.acquired || !g_sadc.initialized)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  /* This exactly selects the hardware's uncalibrated output. It neither reads
   * nor writes OTP/eFuse calibration data. */
  modifyreg32(SADC_STEADY_CTRL, 0, SADC_STEADY_BYPASS);
  return BK_OK;
}

bk_err_t bk_adc_start(void)
{
  if (!g_sadc.acquired || !g_sadc.initialized || !g_sadc.configured)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  if ((getreg32(SADC_CTRL) & SADC_CTRL_BUSY) != 0)
    {
      return BK_ERR_ADC_BUSY;
    }

  modifyreg32(SADC_CTRL, 0, SADC_CTRL_ENABLE);
  g_sadc.started = true;
  return BK_OK;
}

bk_err_t bk_adc_stop(void)
{
  if (!g_sadc.acquired || !g_sadc.initialized)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  modifyreg32(SADC_CTRL, SADC_CTRL_ENABLE, 0);
  modifyreg32(SADC_CTRL, 0, SADC_CTRL_INT_CLEAR);
  g_sadc.started = false;
  return BK_OK;
}

bk_err_t bk_adc_read_raw(uint16_t *read_buf, uint32_t size, uint32_t timeout)
{
  clock_t start;
  clock_t limit;
  uint32_t samples = 0;

  if (read_buf == NULL || size == 0)
    {
      return BK_ERR_PARAM;
    }

  if (!g_sadc.acquired || !g_sadc.initialized || !g_sadc.started)
    {
      return BK_ERR_ADC_NOT_INIT;
    }

  /* Armino's timeout is expressed in milliseconds. Polling avoids registering
   * a shared SARADC IRQ solely for the PHY's short calibration collection. */
  start = clock_systime_ticks();
  limit = MSEC2TICK(timeout);
  do
    {
      if ((getreg32(SADC_CTRL) & SADC_CTRL_FIFO_EMPTY) == 0)
        {
          /* adc_driver.c drains adc_data (not fifo_data) in continuous mode. */
          read_buf[samples++] = (uint16_t)getreg32(SADC_DATA);
          if (samples == size)
            {
              return BK_OK;
            }
          continue;
        }

      up_udelay(10);
    }
  while ((clock_systime_ticks() - start) < limit);

  syslog(LOG_ERR, "[%s] read timeout samples=%lu requested=%lu timeout=%lu ms\n",
         SADC_TAG, (unsigned long)samples, (unsigned long)size,
         (unsigned long)timeout);
  return BK_ERR_ADC_GET_READ_SEMA;
}

#endif /* moved SARADC fallback */
