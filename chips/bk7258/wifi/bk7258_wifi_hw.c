/*
 * chips/bk7258/wifi/bk7258_wifi_hw.c
 *
 * BK7258 Wi-Fi clock/power/IRQ glue.
 *
 * Uses team-owned SYS clock and power-gate helpers. This deliberately stops
 * before MAC/PHY reset, RF calibration, or vendor initialization: those need
 * additional hardware contracts and remain outside the runtime-off stage.
 *
 * Register evidence (plan §11.2, Armino release/v3.1.1):
 *   secure SYS base            0x44010000
 *   MAC/BKRW                   0x4a010000
 *   XVR                        0x4a800000
 *   AGC                        0x4980a000
 *   RC                         0x4980c000
 *   TRX                        0x4980c200
 *   power table                0x4980c400
 *   DPD                        0x49840000
 *   MAC power domain           9  (WIFIP_MAC)
 *   PHY power domain           10 (WIFI_PHY)
 *   MAC clock gate             SYS_CPU_DEVICE_CLK_ENABLE bit 26
 *   PHY clock gate             SYS_CPU_DEVICE_CLK_ENABLE bit 27
 */

#include <nuttx/config.h>

#include <errno.h>

#include <arch/chip/bk7258_clock.h>
#include <arch/chip/bk7258_sysctrl.h>

#include "bk7258_wifi_internal.h"

int bk7258_wifi_hw_init(void)
{
  int ret;

  ret = bk7258_wifi_hw_power_on();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_hw_clock_on();
  if (ret < 0)
    {
      bk7258_wifi_hw_power_off();
      return ret;
    }

  return 0;
}

void bk7258_wifi_hw_deinit(void)
{
  bk7258_wifi_hw_clock_off();
  bk7258_wifi_hw_power_off();
}

int bk7258_wifi_hw_power_on(void)
{
  int ret;

  ret = bk7258_phy_power(true);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_mac_power(true);
  if (ret < 0)
    {
      bk7258_phy_power(false);
      return ret;
    }

  /* The NX MAC core sits behind the OFDM domain; without it crm_mdm_reset()
   * in the pinned archive skips the modem reset release and the MAC state
   * machine never leaves 0. */
  ret = bk7258_ofdm_power(true);
  if (ret < 0)
    {
      bk7258_mac_power(false);
      bk7258_phy_power(false);
      return ret;
    }

  return 0;
}

int bk7258_wifi_hw_power_off(void)
{
  int ret;
  int phy_ret;

  ret = bk7258_ofdm_power(false);
  phy_ret = bk7258_mac_power(false);
  if (ret >= 0)
    {
      ret = phy_ret;
    }
  phy_ret = bk7258_phy_power(false);
  return ret < 0 ? ret : phy_ret;
}

int bk7258_wifi_hw_clock_on(void)
{
  int ret;

  ret = bk7258_phy_clock(true);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_mac_clock(true);
  if (ret < 0)
    {
      bk7258_phy_clock(false);
      return ret;
    }

  return 0;
}

int bk7258_wifi_hw_clock_off(void)
{
  int ret;
  int phy_ret;

  ret = bk7258_mac_clock(false);
  phy_ret = bk7258_phy_clock(false);
  return ret < 0 ? ret : phy_ret;
}
