/*
 * chips/bk7258/wifi/glue/hw_driver_shim.c
 *
 * NuttX implementation of the Armino sys_ctrl / sys_ll / gpio / ckmn / aon-rtc
 * callbacks the vendored glue registers into the Wi-Fi driver capability
 * table. Interrupt-enable and modem-clock callbacks map onto the team chip
 * layer register bits (bk7258_memorymap.h); power/OFDM/RC32k/GPIO remain
 * skeleton no-ops until the corresponding team drivers exist.
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <stdint.h>
#include <stdbool.h>

#include <syslog.h>

#include <arm_internal.h>

#include <nuttx/spinlock.h>
#include <bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include "sys_driver.h"
#include "sys_ll.h"
#include "gpio_driver.h"
#include "driver/ckmn.h"
#include "driver/aon_rtc.h"
#include "aon_pmu_hal.h"
#include "sys_types.h"
#include "driver/int.h"
#include <bk7258_irq.h>

struct bk7258_wifi_isr_slot
{
  int_group_isr_t callback;
  void *arg;
};

static struct bk7258_wifi_isr_slot g_wifi_isr[64];

static int bk7258_wifi_isr_trampoline(int irq, void *context, void *arg)
{
  struct bk7258_wifi_isr_slot *slot = arg;
  (void)irq;
  (void)context;
  if (slot != NULL && slot->callback != NULL)
    {
      slot->callback(slot->arg);
    }
  return OK;
}

bk_err_t bk_int_isr_register(icu_int_src_t src, int_group_isr_t callback,
                             void *arg)
{
  int ret;

  if (src >= 64 || callback == NULL)
    {
      return BK_ERR_PARAM;
    }

  if (g_wifi_isr[src].callback != NULL)
    {
      bk7258_icu_disable(src);
      irq_detach(16 + src);
    }

  g_wifi_isr[src].callback = callback;
  g_wifi_isr[src].arg = arg;
  ret = bk7258_icu_attach(src, bk7258_wifi_isr_trampoline,
                          &g_wifi_isr[src]);
  if (ret < 0)
    {
      g_wifi_isr[src].callback = NULL;
      g_wifi_isr[src].arg = NULL;
      return BK_FAIL;
    }

  return bk7258_icu_enable(src) == OK ? BK_OK : BK_FAIL;
}

bk_err_t bk_wifi_interrupt_init(void)
{
  uint32_t mask = WIFI_MAC_GEN_INT_BIT |
                  WIFI_MAC_PORT_TRIGGER_INT_BIT |
                  WIFI_MAC_TX_TRIGGER_INT_BIT |
                  WIFI_MAC_RX_TRIGGER_INT_BIT |
                  WIFI_MAC_TX_RX_MISC_INT_BIT |
                  WIFI_MAC_TX_RX_TIMER_INT_BIT;

  return sys_drv_int_enable(mask) == 0 ? BK_OK : BK_FAIL;
}

/* These are read-only AON values whose register locations are owned by the
 * chip layer.  Keep the vendor-facing names here so the PHY adapter can use
 * the proven chip accessors without inventing an ADC calibration value. */
uint32_t aon_pmu_drv_get_adc_cal(void)
{
  uint32_t value;
  static bool reported;

  if (bk7258_pmu_get_adc_cal(&value) != OK)
    {
      return 0;
    }

  if (!reported)
    {
      reported = true;
      syslog(LOG_INFO, "[BK7258-WIFI] AON ADC trim=%lu\n",
             (unsigned long)value);
    }

  return value;
}

uint32_t aon_pmu_drv_bias_cal_get(void)
{
  uint32_t value;
  static bool reported;

  if (bk7258_pmu_get_bgcal(&value) != OK)
    {
      return 0;
    }

  if (!reported)
    {
      reported = true;
      syslog(LOG_INFO, "[BK7258-WIFI] AON bias trim=%lu (PMU R7E.cbcal)\n",
             (unsigned long)value);
    }

  return value;
}

uint32_t aon_pmu_hal_reg_get(pmu_reg_e reg)
{
  unsigned int address;
  uint32_t value;

  switch (reg)
    {
      case PMU_REG0:   address = 0x00; break;
      case PMU_REG1:   address = 0x01; break;
      case PMU_REG2:   address = 0x02; break;
      case PMU_REG3:   address = 0x03; break;
      case PMU_REG0x25: address = 0x25; break;
      case PMU_REG0x40: address = 0x40; break;
      case PMU_REG0x41: address = 0x41; break;
      case PMU_REG0x42: address = 0x42; break;
      case PMU_REG0x43: address = 0x43; break;
      case PMU_REG0x70: address = 0x70; break;
      case PMU_REG0x71: address = 0x71; break;
      case PMU_REG0x7c: address = 0x7c; break;
      default: return 0;
    }

  return bk7258_pmu_read(address, &value) == OK ? value : 0;
}

uint32_t aon_pmu_hal_get_chipid(void)
{
  uint32_t value;
  return bk7258_pmu_get_chipid(&value) == OK ? value : 0;
}

/****************************************************************************
 * Interrupt enable (SYS_CPU0_INT_EN / INT_EN_HI)
 ****************************************************************************/

int32_t sys_drv_int_enable(uint32_t param)
{
  modifyreg32(BK7258_SYS_CPU0_INT_EN, param, param);
  return 0;
}

int32_t sys_drv_int_disable(uint32_t param)
{
  modifyreg32(BK7258_SYS_CPU0_INT_EN, param, 0);
  return 0;
}

int32_t sys_drv_int_group2_enable(uint32_t param)
{
  modifyreg32(BK7258_SYS_CPU0_INT_EN_HI, param, param);
  return 0;
}

int32_t sys_drv_int_group2_disable(uint32_t param)
{
  modifyreg32(BK7258_SYS_CPU0_INT_EN_HI, param, 0);
  return 0;
}

/****************************************************************************
 * Modem / MAC / PHY clock (SYS_DEV_CLK_EN)
 ****************************************************************************/

uint32_t sys_drv_modem_bus_clk_ctrl(bool clk_en)
{
  uint32_t v = clk_en ? BK7258_SYS_MAC_CKEN : 0;

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_MAC_CKEN, v);
  return 0;
}

uint32_t sys_drv_modem_clk_ctrl(bool clk_en)
{
  uint32_t v = clk_en ? BK7258_SYS_PHY_CKEN : 0;

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_PHY_CKEN, v);
  return 0;
}

/****************************************************************************
 * sys_ll clock-enable state
 ****************************************************************************/

uint32_t sys_ll_get_cpu_device_clk_enable_mac_cken(void)
{
  return (getreg32(BK7258_SYS_DEV_CLK_EN) & BK7258_SYS_MAC_CKEN) ? 1 : 0;
}

uint32_t sys_ll_get_cpu_device_clk_enable_phy_cken(void)
{
  return (getreg32(BK7258_SYS_DEV_CLK_EN) & BK7258_SYS_PHY_CKEN) ? 1 : 0;
}

/****************************************************************************
 * Module power state (skeleton)
 ****************************************************************************/

int32_t sys_drv_module_power_state_get(power_module_name_t module)
{
  (void)module;
  return PM_POWER_MODULE_STATE_ON;
}

/****************************************************************************
 * OFDM sleep/wakeup power
 *
 * Real register semantics: the OFDM module-power bit lives in
 * SYS_POWER_WAKEUP (bit 13, POWER_MODULE_NAME_OFDM).  The pinned libwifi
 * crm_mdm_reset() polls this getter before releasing the modem reset, so a
 * cached skeleton value here silently prevented the NX MAC core from ever
 * leaving reset.  1 = domain powered down, 0 = powered, matching the
 * low-active POWER_WAKEUP register and the module-power bit numbering.
 ****************************************************************************/

void sys_ll_set_cpu_power_sleep_wakeup_pwd_ofdm(uint32_t v)
{
  /* Board evidence (2026-08-31): once the PS stub group was bound with
   * authoritative semantics, the pinned archive's sleep path legitimately
   * called this setter with v=1 between init and scan, powering the OFDM
   * domain down (POWER_WAKEUP 0x70000000 -> 0x70002000, CRM modem field
   * 0x3108 -> 0x0108) and parking the NX MAC core at FSM 0.  This profile
   * has no power-save implementation, so a down-vote from the archive is
   * explicitly refused and reported; the power-up direction stays real. */
  if (v)
    {
      static bool reported;

      if (!reported)
        {
          reported = true;
          syslog(LOG_WARNING, "[BK7258-WIFI] capability: OFDM domain power-down refused"
                    " (power-save not implemented)\n");
        }

      return;
    }

  static spinlock_t lock = SP_UNLOCKED;
  irqstate_t flags = spin_lock_irqsave(&lock);
  uint32_t reg = getreg32(BK7258_SYS_POWER_WAKEUP);

  reg &= ~BK7258_SYS_OFDM_POWERDOWN;
  putreg32(reg, BK7258_SYS_POWER_WAKEUP);
  spin_unlock_irqrestore(&lock, flags);
}

uint32_t sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm(void)
{
  return (getreg32(BK7258_SYS_POWER_WAKEUP) & BK7258_SYS_OFDM_POWERDOWN) ? 1 : 0;
}

/****************************************************************************
 * GPIO device mapping (skeleton no-op)
 ****************************************************************************/

bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev)
{
  (void)gpio_id;
  (void)dev;
  return BK_OK;
}

bk_err_t gpio_dev_unmap(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return BK_OK;
}

/****************************************************************************
 * CKMN (skeleton)
 ****************************************************************************/

bk_err_t bk_ckmn_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_ckmn_driver_deinit(void)
{
  return BK_OK;
}

bk_err_t bk_ckmn_driver_get_rc32k_ppm(void)
{
  return 0;
}

/****************************************************************************
 * AON RTC (skeleton)
 ****************************************************************************/

bk_err_t bk_aon_rtc_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_aon_rtc_driver_deinit(void)
{
  return BK_OK;
}
