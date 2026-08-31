/* hal_port_lpdoze.c - PM/LPO/doze-family functions ported verbatim from
 * the authoritative Armino sources so the g_wifi_os_funcs answers are
 * byte-for-byte identical to the reference environment.
 *
 * Sources (function bodies copied, only the wrappers renamed):
 *   - cp/middleware/soc/bk7258/hal/sys_ll.h:      LL analog/ANA primitives
 *   - cp/middleware/soc/bk7258/soc/aon_pmu_ll.h:  R41 lpo_config/wakeup_ena
 *   - cp/middleware/soc/bk7258/hal/sys_hal.c:     DPLL SPI sequencer,
 *                                                 xtalh_ctune
 *   - cp/middleware/soc/bk7258/hal/sys_pm_hal.c:  mac wakeup source
 *   - cp/middleware/soc/bk7258/hal/aon_pmu_hal.c: wakeup source clear
 *   - cp/middleware/driver/sys_ctrl/sys_wifi_driver.c:
 *                                                 sys_drv_cali_dpll and
 *                                                 its delay helpers
 *   - cp/middleware/driver/pmu/aon_pmu_driver.c:  clear_wakeup_source
 */

#include <stdint.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <syslog.h>

#include <arch/chip/bk7258_memorymap.h>
#include "armino_compat.h"

/* ------------------------------------------------------------------ */
/* LL: analog register window (sys_ll.h)                               */
/* ------------------------------------------------------------------ */

/* sys_ll_set_analog_reg_value: write then spin on the analog-bus SPI
 * busy bit for this register index.  (Armino spins forever; we cap at
 * 1000 iterations and log -- a stuck busy bit is a real fault.) */
static void hp_sys_ll_set_analog_reg_value(uint32_t addr, uint32_t value)
{
  uint32_t idx = HP_GET_SYS_ANALOG_REG_IDX(addr);
  uint32_t count;

  HP_REG_WRITE(addr, value);

  for (count = 0; count < 1000; count++)
    {
      if ((HP_REG_READ(HP_SYS_ANALOG_REG_SPI_STATE_REG) &
           (UINT32_C(1) << HP_SYS_ANALOG_REG_SPI_STATE_POS(idx))) == 0)
        {
          return;
        }
    }

  syslog(LOG_ERR, "[HP] ana write idx=%lu busy timeout\n",
         (unsigned long)idx);
}

/* sys_set_ana_reg_bit: read-modify-write one field of an ANA register. */
static void hp_sys_set_ana_reg_bit(uint32_t reg_addr, uint32_t pos,
                                   uint32_t mask, uint32_t value)
{
  uint32_t reg_value = *(volatile uint32_t *)(reg_addr);

  reg_value &= ~(mask << pos);
  reg_value |= ((value & mask) << pos);
  hp_sys_ll_set_analog_reg_value(reg_addr, reg_value);
}

/* sys_ll_set_ana_reg0_spitrig (sys_ll.h:6085): bit19 of ANA_REG0. */
void hp_sys_ll_set_ana_reg0_spitrig(uint32_t v)
{
  hp_sys_set_ana_reg_bit(HP_SOC_SYS_REG_BASE + (0x40u << 2), 19, 0x1u, v);
}

/* sys_ll_set_ana_reg0_spideten (sys_ll.h, ANA_REG0 bit4). */
void hp_sys_ll_set_ana_reg0_spideten(uint32_t v)
{
  hp_sys_set_ana_reg_bit(HP_SOC_SYS_REG_BASE + (0x40u << 2), 4, 0x1u, v);
}

/* ------------------------------------------------------------------ */
/* LL: AON PMU R41 lpo_config / wakeup_ena (aon_pmu_ll.h)              */
/* ------------------------------------------------------------------ */

uint32_t hp_aon_pmu_ll_get_r41_lpo_config(void)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(HP_SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  return r->lpo_config;
}

void hp_aon_pmu_ll_set_r41_lpo_config(uint32_t v)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(HP_SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  r->lpo_config = v;
}

uint32_t hp_aon_pmu_ll_get_r41_wakeup_ena(void)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(HP_SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  return r->wakeup_ena;
}

void hp_aon_pmu_ll_set_r41_wakeup_ena(uint32_t v)
{
  hp_aon_pmu_r41_t *r = (hp_aon_pmu_r41_t *)(HP_SOC_AON_PMU_REG_BASE +
                                             (0x41u << 2));
  r->wakeup_ena = v;
}

/* ------------------------------------------------------------------ */
/* DRV: delay helpers (sys_wifi_driver.c, constants as upstream)       */
/* ------------------------------------------------------------------ */

#define HP_SYS_DRV_DELAY_TIME_10US   120u   /* SYS_DRV_DELAY_TIME_10US */
#define HP_SYS_DRV_DELAY_TIME_200US  3400u  /* SYS_DRV_DELAY_TIME_200US */

void hp_sys_drv_delay10us(void)
{
  volatile uint32_t i;

  for (i = 0; i < HP_SYS_DRV_DELAY_TIME_10US; i++)
    ;
}

void hp_sys_drv_delay200us(void)
{
  volatile uint32_t i;

  for (i = 0; i < HP_SYS_DRV_DELAY_TIME_200US; i++)
    ;
}

void hp_sys_drv_ps_dpll_delay(uint32_t time)
{
  volatile uint32_t i;

  for (i = 0; i < time; i++)
    ;
}

/* ------------------------------------------------------------------ */
/* HAL: DPLL calibration SPI sequencer (sys_hal.c:1751-1769)           */
/* ------------------------------------------------------------------ */

void hp_sys_hal_cali_dpll_spi_trig_disable(void)
{
  hp_sys_ll_set_ana_reg0_spitrig(0);
}

void hp_sys_hal_cali_dpll_spi_trig_enable(void)
{
  hp_sys_ll_set_ana_reg0_spitrig(1);
}

void hp_sys_hal_cali_dpll_spi_detect_disable(void)
{
  hp_sys_ll_set_ana_reg0_spideten(0);
}

void hp_sys_hal_cali_dpll_spi_detect_enable(void)
{
  hp_sys_ll_set_ana_reg0_spideten(1);
}

/* ------------------------------------------------------------------ */
/* DRV: sys_drv_cali_dpll (sys_wifi_driver.c:49, full upstream body)   */
/* ------------------------------------------------------------------ */

uint32_t hp_sys_drv_cali_dpll(uint32_t param)
{
  irqstate_t int_level = enter_critical_section();

  hp_sys_hal_cali_dpll_spi_trig_disable();

  if (!param)
    {
      hp_sys_drv_delay10us();
    }
  else
    {
      hp_sys_drv_ps_dpll_delay(60);
    }

  hp_sys_hal_cali_dpll_spi_trig_enable();
  hp_sys_hal_cali_dpll_spi_detect_disable();

  if (!param)
    {
      hp_sys_drv_delay200us();
    }
  else
    {
      hp_sys_drv_ps_dpll_delay(340);
    }

  hp_sys_hal_cali_dpll_spi_detect_enable();

  leave_critical_section(int_level);
  return 0;
}

/* ------------------------------------------------------------------ */
/* HAL/PM: MAC wakeup source (sys_pm_hal.c:1249 / aon_pmu_hal.c:49)    */
/* wakeup_source_t: GPIO=0 RTC=1 WIFI=2 BT=3 USBPLUG=4 TOUCHED=5       */
/* ------------------------------------------------------------------ */

void hp_sys_hal_enable_mac_wakeup_source(void)
{
  uint32_t wakeup_ena = hp_aon_pmu_ll_get_r41_wakeup_ena();

  wakeup_ena |= (UINT32_C(1) << 2); /* WAKEUP_SOURCE_INT_WIFI */
  hp_aon_pmu_ll_set_r41_wakeup_ena(wakeup_ena);
}

void hp_aon_pmu_hal_clear_wakeup_source(uint32_t value)
{
  uint32_t wakeup_source = hp_aon_pmu_ll_get_r41_wakeup_ena();

  wakeup_source &= ~(UINT32_C(0x1) << value);
  hp_aon_pmu_ll_set_r41_wakeup_ena(wakeup_source);
}
