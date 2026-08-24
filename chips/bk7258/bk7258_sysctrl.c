/****************************************************************************
 * chips/bk7258/bk7258_sysctrl.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_sysctrl.h"

int bk7258_analog_read(unsigned int reg, uint32_t *value);
int bk7258_analog_write(unsigned int reg, uint32_t value);

static int bk7258_pmu_valid(unsigned int reg)
{
  return reg < 0x80 ? OK : -EINVAL;
}

static int bk7258_analog_update_bits(unsigned int reg, uint32_t mask,
                                     uint32_t value)
{
  uint32_t current;
  int ret;

  ret = bk7258_analog_read(reg, &current);
  if (ret < 0)
    {
      return ret;
    }

  current = (current & ~mask) | (value & mask);
  return bk7258_analog_write(reg, current);
}

int bk7258_pmu_read(unsigned int reg, uint32_t *value)
{
  if (value == NULL || bk7258_pmu_valid(reg) < 0)
    {
      return -EINVAL;
    }

  *value = getreg32(BK7258_AON_PMU_BASE + (reg << 2));
  return OK;
}

int bk7258_pmu_write(unsigned int reg, uint32_t value)
{
  if (bk7258_pmu_valid(reg) < 0)
    {
      return -EINVAL;
    }

  putreg32(value, BK7258_AON_PMU_BASE + (reg << 2));
  return OK;
}

int bk7258_analog_read(unsigned int reg, uint32_t *value)
{
  if (value == NULL || reg >= 28)
    {
      return -EINVAL;
    }

  *value = getreg32(BK7258_SYS_ANALOG_BASE + (reg << 2));
  return OK;
}

int bk7258_analog_write(unsigned int reg, uint32_t value)
{
  unsigned int count;

  if (reg >= 28)
    {
      return -EINVAL;
    }

  putreg32(value, BK7258_SYS_ANALOG_BASE + (reg << 2));
  for (count = 0; count < 1000; count++)
    {
      if ((getreg32(BK7258_SYS_ANALOG_STATE) &
           (UINT32_C(1) << (BK7258_SYS_ANALOG_STATE_SHIFT + reg))) == 0)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

int bk7258_pmu_get_chipid(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_pmu_read(0x7c, &raw);
  if (ret == OK && value != NULL)
    {
      *value = raw;
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_pmu_get_adc_cal(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_pmu_read(0x7d, &raw);
  if (ret == OK && value != NULL)
    {
      *value = (raw >> 9) & UINT32_C(0x3f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_pmu_get_bgcal(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_pmu_read(0x7d, &raw);
  if (ret == OK && value != NULL)
    {
      *value = (raw >> 15) & UINT32_C(0x3f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_sys_get_bgcalm(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_analog_read(8, &raw);
  if (ret == OK && value != NULL)
    {
      *value = (raw >> 22) & UINT32_C(0x3f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_sys_set_bgcalm(uint32_t value)
{
  int ret;

  if (value > UINT32_C(0x3f))
    {
      return -EINVAL;
    }

  ret = bk7258_analog_update_bits(9, UINT32_C(1) << 9,
                                  UINT32_C(1) << 9);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_analog_update_bits(8, UINT32_C(0x3f) << 22,
                                  value << 22);
  /* Always release the latch, including on a failed analog transaction. */
  if (bk7258_analog_update_bits(9, UINT32_C(1) << 9, 0) < 0 && ret == OK)
    {
      ret = -EIO;
    }
  return ret;
}

int bk7258_dpll_enable(bool enable)
{
  return bk7258_analog_update_bits(5, UINT32_C(1) << 5,
                                   enable ? UINT32_C(1) << 5 : 0);
}

static int bk7258_power_gate(uint32_t mask, bool enable)
{
  irqstate_t flags;
  int ret;

  /* The Armino register is low-active: 1 means power down. */

  flags = enter_critical_section();
  modifyreg32(BK7258_SYS_POWER_WAKEUP, mask, enable ? 0 : mask);
  ret = ((getreg32(BK7258_SYS_POWER_WAKEUP) & mask) == 0) == enable ?
        OK : -EIO;
  leave_critical_section(flags);
  syslog(LOG_INFO, "[BK7258] power gate mask=0x%08lx enable=%d ret=%d reg=0x%08lx\n",
         (unsigned long)mask, enable, ret,
         (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP));
  return ret;
}

int bk7258_mac_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_MAC_POWERDOWN, enable);
}

int bk7258_phy_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_PHY_POWERDOWN, enable);
}

int bk7258_mac_reset(void)
{
  /* Armino sys_hal_mac_subsys_reset() is explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] MAC reset unsupported\n");
  return -ENOTSUP;
}

int bk7258_phy_reset(void)
{
  /* Armino sys_hal_modem_core/subsys_reset() are explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] PHY reset unsupported\n");
  return -ENOTSUP;
}
