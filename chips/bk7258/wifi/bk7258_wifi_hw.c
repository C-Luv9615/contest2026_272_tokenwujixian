/*
 * chips/bk7258/wifi/bk7258_wifi_hw.c
 *
 * BK7258 Wi-Fi clock/power/IRQ glue.
 *
 * Skeleton status: register evidence is recorded as comments; the actual
 * MMIO writes must reuse the chip-level SYS/ICU helpers (bk7258_clock.c,
 * bk7258_irq.c) after the secure/non-secure SYS base and the power-domain
 * read-modify-write semantics are validated on hardware. Until then every
 * operation returns -ENOSYS, not a silent no-op.
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

#include "bk7258_wifi_internal.h"

int bk7258_wifi_hw_init(void)
{
  return -ENOSYS;
}

void bk7258_wifi_hw_deinit(void)
{
}

int bk7258_wifi_hw_power_on(void)
{
  /* Vendor power-domain vote (WIFIP_MAC + WIFI_PHY + PHY_WIFI) then RF vote.
   * Not wired: needs the chip power-domain helper and validated polarity. */
  return -ENOSYS;
}

int bk7258_wifi_hw_power_off(void)
{
  return -ENOSYS;
}

int bk7258_wifi_hw_clock_on(void)
{
  /* Read-modify-write SYS_CPU_DEVICE_CLK_ENABLE MAC bit 26 and PHY bit 27,
   * preserving unrelated bits. Not wired until the secure SYS base is fixed. */
  return -ENOSYS;
}

int bk7258_wifi_hw_clock_off(void)
{
  return -ENOSYS;
}
