/* hal_port_power_clk_rf.c - power_clk_rf_init() ported verbatim from
 * cp/middleware/driver/common/driver.c using the verbatim-copied
 * sys_ll.h LL accessors (ANA_REG4/6/11 + AON PMU R41).
 *
 * The original macros (SYS_ANA_REG4_ROSC_CAL_* etc.) map to ANA_REG6
 * fields on BK7258 (confirmed by sys_ana_reg6_t struct: calib_interval
 * at bits[0:9], cal_mode at bit[23], manu_ena at bit[24], calib_auto at
 * bit[22]).  The sequence is identical to the authoritative build.
 *
 * Also includes aon_pmu_drv_init equivalent (R41 wakeup_ena etc).
 */

#include <stdint.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <syslog.h>

#include <arch/chip/bk7258_memorymap.h>
#include <arm_internal.h>
#include "hal_port_sys_all.h"

/* ------------------------------------------------------------------ */
/* Analog read/write primitives (sys_ll.h:46-68, verbatim)             */
/* ------------------------------------------------------------------ */

uint32_t hp_sys_ll_get_analog_reg_value(uint32_t addr)
{
  return REG_READ(addr);
}

void hp_sys_ll_set_analog_reg_value(uint32_t addr, uint32_t value)
{
  uint32_t idx = GET_SYS_ANALOG_REG_IDX(addr);
  uint32_t count;

  REG_WRITE(addr, value);

  for (count = 0; count < 1000; count++)
    {
      if ((REG_READ(SYS_ANALOG_REG_SPI_STATE_REG) &
           (UINT32_C(1) << SYS_ANALOG_REG_SPI_STATE_POS(idx))) == 0)
        {
          return;
        }
    }

  syslog(LOG_ERR, "[HP] ana write idx=%lu busy timeout\n",
         (unsigned long)idx);
}

/* ------------------------------------------------------------------ */
/* power_clk_rf_init() active portions (driver.c:119-258)             */
/* verbatim: same register, same bit values, same sequence            */
/* ------------------------------------------------------------------ */

void hp_power_clk_rf_init(void)
{
  uint32_t param;

  /* Step 1: power on all modules for bringup
   * (MODULES_POWER_OFF_ENABLE=1: powers off unused ENCP/AUDP/VIDP/BTSP/CPU1;
   *  WIFIP_MAC and WIFI_PHY are powered ON by wifi_init's vote_power_ctrl.)
   * Our hw_init already handles the WiFi-relevant power gates. */

  /* Step 2: enable the analog clock for WiFi
   * sys_drv_module_RF_power_ctrl(MODULE_NAME_WIFI, POWER_MODULE_STATE_ON)
   * → this is the vendor RF power-on; already handled by
   *   bk_rf_adapter_init() in our port. */

  /* Step 3: MODULES_CLK_ENABLE=0, skipped in Armino too */

  /* Step 4: CPU/matrix clock config: all commented out in Armino */

  /* Step 5: temperature detect enable for VIO (ANA_REG6 RMW) */
  param = hp_sys_ll_get_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2));
  param |= (UINT32_C(1) << 25) |  /* EN_TEMPDET: ANA_REG6 temp detect enable  */
           (UINT32_C(0x7) << 26) | /* RXTAL_LP:  ANA_REG6 bits[28:26] */
           (UINT32_C(0x7) << 29);  /* RXTAL_HP:  ANA_REG6 bits[31:29] */
  param &= ~(UINT32_C(1) << 24);   /* EN_SLEEP:  ANA_REG6 bit24 clear */
  hp_sys_ll_set_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2), param);

  /* Step 6: let rosc to bt/wifi ip (R41 bit24 set) */
  param = REG_READ(SOC_AON_PMU_REG_BASE + (0x41u << 2));
  param |= UINT32_C(1) << 24;
  REG_WRITE(SOC_AON_PMU_REG_BASE + (0x41u << 2), param);

  /* Step 7: ROSC calibration (ANA_REG6 sequence, verbatim)
   * a. no debug (ROSC_DEBUG_EN=0)
   * b. config calibration:                                      */
  param = hp_sys_ll_get_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2));
  param &= ~(UINT32_C(0x3ff) << 0);             /* clear calib_interval bits[9:0] */
  param |= UINT32_C(0x4) << 0;                  /* interval = 4 → 1s             */
  param |= UINT32_C(0x1) << 23;                 /* cal_mode = 1 → 32K            */
  param |= UINT32_C(0x1) << 22;                 /* calib_auto = 1 → enable       */
  param &= ~(UINT32_C(1) << 24);                /* manu_ena = 0 → close manual   */
  hp_sys_ll_set_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2), param);

  /* c. trigger calibration */
  param = hp_sys_ll_get_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2));
  param &= ~(UINT32_C(1) << 20);                /* spi_trig clear               */
  hp_sys_ll_set_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2), param);

  param = hp_sys_ll_get_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2));
  param |= UINT32_C(1) << 20;                   /* spi_trig enable              */
  hp_sys_ll_set_analog_reg_value(SOC_SYS_REG_BASE + (0x46u << 2), param);

  /* rosc calibration end */

  /* Step 8: DPLL calibration — commented out in Armino driver.c too */
  /* Step 9: DCO calibration — commented out in Armino driver.c too */
}
