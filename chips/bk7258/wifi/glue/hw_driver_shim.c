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

#include <stdint.h>
#include <stdbool.h>

#include <bk7258_memorymap.h>

#include "sys_driver.h"
#include "sys_ll.h"
#include "gpio_driver.h"
#include "driver/ckmn.h"
#include "driver/aon_rtc.h"

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
 * OFDM sleep/wakeup power (skeleton — register not yet pinned)
 ****************************************************************************/

static uint32_t g_bk7258_ofdm_pwd;

void sys_ll_set_cpu_power_sleep_wakeup_pwd_ofdm(uint32_t v)
{
  g_bk7258_ofdm_pwd = v;
}

uint32_t sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm(void)
{
  return g_bk7258_ofdm_pwd;
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
