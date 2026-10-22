/****************************************************************************
 * chips/bk7258/bk7258_reset_reason.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Armino reset-reason and reboot face
 *
 * Provides the 11 symbols of middleware/driver/reset_reason/reset_reason.c
 * and the 2 of components/bk_system/reboot.c.
 *
 * Contract notes that a plain re-implementation would get wrong:
 *
 *   - The reset reason is NOT read from a register on demand.  It is latched
 *     once by reset_reason_init() into s_start_type, and the hardware copy is
 *     immediately overwritten with RESET_SOURCE_POWERON so that a later reset
 *     cannot inherit it.  bk_misc_get_reset_reason() returns the cached
 *     value.
 *
 *   - The storage is AON PMU R0: CP in bits[4:11] (8 bits, values > 0xff are
 *     rejected), AP in bits[24:30] (7 bits, > 0x7f rejected).  The read side
 *     uses aon_pmu_ll_get_r7a() and the write side aon_pmu_hal_get_r0() /
 *     aon_pmu_hal_set_r0(); R0 and R7A are the same field bank seen through
 *     the HAL and LL names.
 *
 *   - persist_memory_* uses a fixed address (PERSIST_MEMORY_ADDR, 0x20003FFC
 *     with CONFIG_SPE) outside the NuttX SRAM window; it survives a watchdog
 *     reset and is the baseline's "did we lose the last crash" marker.
 *     Nothing references it today; it is implemented anyway.
 *
 *   - bk_reboot_ex() records the reason, waits 100 ms (so a pending
 *     bk_writer BEKEN_DO_REBOOT command can drain), drops the CPU to 60 MHz,
 *     disables interrupts, records the reason a second time (the first write
 *     can be lost if the PMU was mid-update), and then resets the chip
 *     through the AON WDT.  It never returns.  The entry-count guard keeps a
 *     second entry (fault during the first) from repeating the delay.
 *
 * CP only: the baseline include directories this file needs are added for
 * the CP component (chips/bk7258/CMakeLists.txt).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <os/os.h>
#include <components/log.h>
#include <components/system.h>
#include <driver/gpio.h>
#include <driver/pwr_clk.h>
#include <driver/wdt.h>
#include <modules/pm.h>
#include "aon_pmu_hal.h"
#include "aon_pmu_ll.h"
#include "aon_pmu_driver.h"
#include "aon_wdt_ll.h"
#include "arch_interrupt.h"
#include "bk_misc.h"
#include "bk7258_reset.h"
#include <reset_reason.h>
#include "wdt_driver.h"
#include "bk_wdt.h"
#include <math.h>
#include <stdio.h>

extern int temp_detect_get_temperature(uint32_t *temperature);
extern bool temp_detect_is_init(void);
bk_err_t bk_sensor_get_current_temperature(float *temperature);
bk_err_t bk_sensor_get_current_voltage(float *voltage);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG "init"
#define DISPLAY_START_TYPE_STR 1

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile bool s_initialized = false;
static uint32_t s_start_type = 0;
static uint32_t s_misc_value_save = 0;
static uint32_t s_mem_value_save = 0;

/****************************************************************************
 * reset_reason.c face
 ****************************************************************************/

uint32_t bk_misc_get_reset_reason(void)
{
  return s_start_type;
}

void persist_memory_init(void)
{
  *((volatile uint32_t *)(PERSIST_MEMORY_ADDR)) =
    (uint32_t)CRASH_ILLEGAL_JUMP_VALUE;
}

uint32_t persist_memory_get(void)
{
  return (*((volatile uint32_t *)(PERSIST_MEMORY_ADDR)));
}

bool persist_memory_is_lost(void)
{
  if ((uint32_t)CRASH_ILLEGAL_JUMP_VALUE == persist_memory_get())
    {
      return false;
    }
  else
    {
      return true;
    }
}

static char *misc_get_start_type_str(uint32_t start_type)
{
#if DISPLAY_START_TYPE_STR
  switch (start_type)
    {
      case RESET_SOURCE_POWERON:
        return "power on";

      case RESET_SOURCE_REBOOT:
        return "software reboot";

      case RESET_SOURCE_WATCHDOG:
        return "interrupt watchdog";

      case RESET_SOURCE_DEEPPS_GPIO:
        return "deep sleep gpio";

      case RESET_SOURCE_DEEPPS_RTC:
        return "deep sleep rtc";

      case RESET_SOURCE_DEEPPS_TOUCH:
        return "deep sleep touch";

      case RESET_SOURCE_CRASH_ILLEGAL_JUMP:
        return "illegal jump";

      case RESET_SOURCE_CRASH_UNDEFINED:
        return "undefined";

      case RESET_SOURCE_CRASH_PREFETCH_ABORT:
        return "prefetch abort";

      case RESET_SOURCE_CRASH_DATA_ABORT:
        return "data abort";

      case RESET_SOURCE_CRASH_UNUSED:
        return "unused";

      case RESET_SOURCE_CRASH_ILLEGAL_INSTRUCTION:
        return "illegal instruction";

      case RESET_SOURCE_CRASH_MISALIGNED:
        return "misaligned";

      case RESET_SOURCE_CRASH_ASSERT:
        return "assert";

      case RESET_SOURCE_DEEPPS_USB:
        return "deep sleep usb";

      case RESET_SOURCE_SUPER_DEEP:
        return "super deep sleep";

      case RESET_SOURCE_NMI_WDT:
        return "nmi watchdog";

      case RESET_SOURCE_HARD_FAULT:
        return "hard fault";

      case RESET_SOURCE_MPU_FAULT:
        return "mpu fault";

      case RESET_SOURCE_BUS_FAULT:
        return "bus fault";

      case RESET_SOURCE_USAGE_FAULT:
        return "usage fault";

      case RESET_SOURCE_SECURE_FAULT:
        return "secure fault";

      case RESET_SOURCE_DEFAULT_EXCEPTION:
        return "default exception";

      case RESET_SOURCE_OTA_REBOOT:
        return "ota reboot";

      case RESET_SOURCE_FORCE_DEEPSLEEP:
        return "enter deep sleep";

      case RESET_SOURCE_UNKNOWN:
      default:
        return "unknown";
    }
#else
  return "";
#endif
}

void show_reset_reason(void)
{
  BK_LOGD(TAG, "reason - %s\r\n", misc_get_start_type_str(s_start_type));
  if (RESET_SOURCE_DEEPPS_GPIO == s_start_type)
    {
#if CONFIG_DEEP_PS
      BK_LOGD(TAG, "by gpio - %d\r\n", bk_misc_wakeup_get_gpio_num());
#else
#ifdef CONFIG_GPIO_DYNAMIC_WAKEUP_SUPPORT
      BK_LOGD(TAG, "by gpio - %d\r\n", bk_gpio_get_wakeup_gpio_id());
#endif
#endif
    }

  BK_LOGD(TAG, "regs - %x, %x, %x\r\n", s_start_type, s_misc_value_save,
          s_mem_value_save);
}

static inline void bk_misc_save_cp_reset_reason(uint32_t type)
{
  FIXED_ADDR_CP_RESET_REASON = type;
}

static inline void bk_misc_save_ap_reset_reason(uint32_t type)
{
  FIXED_ADDR_AP_RESET_REASON = type;
}

uint32_t bk_misc_get_cp_reset_reason(void)
{
  return FIXED_ADDR_CP_RESET_REASON;
}

uint32_t bk_misc_get_ap_reset_reason(void)
{
  return FIXED_ADDR_AP_RESET_REASON;
}

/* aon_pmu_r0_t, the layout these two writers share:
 *
 *   [0]     memchk_bps            [1]     fast_boot
 *   [2]     ota_finish            [3]     bl2_deep_sleep
 *   [4:11]  reset_reason_cp       [12:19] gpio_retention_bitmap
 *   [20:23] reset_count           [24:30] reset_reason_ap
 *   [31]    gpio_sleep
 */

void bk_misc_set_cp_reset_reason(uint32_t type)
{
  if (type > 0xff)
    {
      BK_DUMP_OUT("Invalid cp rr type: 0x%x", type);
      return;
    }

  /* use PMU_REG0 bit[4:11] for reset reason */

  uint32_t misc_value = aon_pmu_hal_get_r0();

  /* clear last reset reason */

  misc_value &= ~(0xff << 4);

  misc_value |= ((type & 0xff) << 4);
  aon_pmu_hal_set_r0(misc_value);
}

void bk_misc_set_ap_reset_reason(uint32_t type)
{
  if (type > 0x7f)
    {
      BK_DUMP_OUT("Invalid ap rr type: 0x%x\r\n", type);
      return;
    }

  /* use PMU_REG0 bit[24:30] for reset reason */

  uint32_t misc_value = aon_pmu_hal_get_r0();

  BK_DUMP_OUT("set ap rr: 0x%x\r\n", type);

  /* clear last reset reason */

  misc_value &= ~(0x7f << 24);

  misc_value |= ((type & 0x7f) << 24);
  aon_pmu_hal_set_r0(misc_value);
}

void bk_misc_set_reset_reason(uint32_t type)
{
  bk_misc_set_cp_reset_reason(type);
}

uint32_t reset_reason_init(void)
{
  uint32_t misc_value;
  uint32_t cp_reset_reason = 0;
  uint32_t ap_reset_reason = 0;

  if (s_initialized != 0)
    {
      return s_start_type;
    }

  misc_value = aon_pmu_ll_get_r7a();
  cp_reset_reason = ((misc_value >> 4) & 0xff);
  ap_reset_reason = ((misc_value >> 24) & 0x7f);

  s_start_type = cp_reset_reason;
  s_misc_value_save = misc_value;

  bk_misc_save_cp_reset_reason(cp_reset_reason);
  bk_misc_save_ap_reset_reason(ap_reset_reason);

  bk_misc_set_cp_reset_reason(RESET_SOURCE_POWERON);
  bk_misc_set_ap_reset_reason(RESET_SOURCE_POWERON);

  arch_init_exception_magic_status();
  s_initialized = true;
  return s_start_type;
}

/****************************************************************************
 * reboot.c face
 ****************************************************************************/

void bk_reboot_ex(uint32_t reset_reason)
{
  static uint32_t entry_cnt = 0;
  if (entry_cnt == 0)
    {
      /* first time come here, or force reboot: avoid these codes cause
       * system abnormal.
       */

      entry_cnt++;

      if (reset_reason < RESET_SOURCE_UNKNOWN)
        {
          bk_misc_set_reset_reason(reset_reason);
        }

      BK_LOGD(TAG, "bk_reboot\r\n");
      delay_ms(100);  /* add delay for bk_writer BEKEN_DO_REBOOT cmd */
      bk_pm_module_vote_cpu_freq(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_60M);

      BK_LOGD(TAG, "wdt reboot\r\n");
      rtos_disable_int();
      if (reset_reason < RESET_SOURCE_UNKNOWN)
        {
          bk_misc_set_reset_reason(reset_reason);
        }
    }

  bk_wdt_force_reboot();
}

void bk_reboot(void)
{
  bk_reboot_ex(RESET_SOURCE_REBOOT);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */

/****************************************************************************
 * Chip reset (moved down from board/bk7258-devkit/src/bk7258_reset.c)
 *
 * The register sequence belongs here, not in the board: this file already
 * carries the baseline headers, and the board layer should only decide
 * *when* to reset and with *which* reason.
 *
 * Every step below is the authority's own call.  The previous board version
 * hand-copied the same registers and got one of them wrong: it wrote AON PMU
 * R0 with a plain store, while aon_pmu_hal_set_r0() follows the store with
 * the two R25 unlock keys (0x424B55AA / 0xBDB4AA55).  Without the keys the
 * write is dropped silently, which is why "reboot bootloader" left
 * cp=0 ap=0 in the bootloader's "!RST" report.
 *
 * The reset itself stays on the AON WDT path (0x5A000A / 0xA5000A), which is
 * what this board has been proven to reset with.  Note that the vendor
 * wdt_hal_force_reboot() would instead take the NMI WDT branch
 * (CONFIG_NMI_WDT_EN=1) and overwrite the warm tag unconditionally, so it is
 * deliberately not used here where the tag has to be cleared.
 ****************************************************************************/

void bk7258_chip_reset(bool enter_bootloader)
{
  if (enter_bootloader)
    {
      /* Cold chain: no warm tag, and RESET_SOURCE_WATCHDOG (0x2) in both
       * domains so the ROM classifies this boot as a watchdog one and BL2
       * opens the serial download handshake window.
       */

      set_reboot_tag(0);
      bk_misc_set_reset_reason(RESET_SOURCE_WATCHDOG);
      bk_misc_set_ap_reset_reason(RESET_SOURCE_WATCHDOG);
    }
  else
    {
      /* Planned reboot: the warm tag fast-jumps back into the running app.
       * RESET_SOURCE_REBOOT is recorded so the next boot can report it --
       * the previous version recorded nothing here, which made every
       * software reboot look like a power-on.
       */

      set_reboot_tag(REBOOT_TAG_REQ);
      bk_misc_set_reset_reason(RESET_SOURCE_REBOOT);
      bk_misc_set_ap_reset_reason(RESET_SOURCE_REBOOT);
    }

  /* Select clk_DIVD as the WDT LPO source so the emergency period bites
   * deterministically, then gate the AON WDT through to the PMU reset line.
   * Both are the vendor driver's own functions (R41 bits[1:0] = 0,
   * R2 bits[5:0] = 0x26); they matched the hand-copied sequence exactly.
   */

  aon_pmu_drv_wdt_change_not_rosc_clk();
  aon_pmu_drv_wdt_rst_dev_enable();

  /* Arm the AON WDT with the mandatory two-write key sequence and the
   * emergency period 0xA, then wait for the PMU to pull the chip down.
   */

  aon_wdt_ll_set_period(0xa);

  for (; ; );
}

/****************************************************************************
 * Probe hooks
 *
 * Called by the platform probe app (app/bk7258_platform_test).  They are not
 * gated here: an unused hook is collected by --gc-sections, and gating them on
 * an app-level config would make the chip layer depend on how it is tested.
 ****************************************************************************/

#include <stdio.h>

void bk7258_wdt_test_peek(void)
{
  printf("wdttest: R0=0x%08lx R7A=0x%08lx cached=0x%02lx"
         " (cp_in_R0=0x%02lx ap_in_R0=0x%02lx)\n",
         (unsigned long)aon_pmu_hal_get_r0(),
         (unsigned long)aon_pmu_ll_get_r7a(),
         (unsigned long)bk_misc_get_reset_reason(),
         (unsigned long)((aon_pmu_hal_get_r0() >> 4) & 0xff),
         (unsigned long)((aon_pmu_hal_get_r0() >> 24) & 0x7f));
}

int bk7258_wdt_test_stop(void)
{
  int ret;

  ret = (int)bk_wdt_stop();
#if CONFIG_TASK_WDT
  bk_task_wdt_stop();
#endif
  return ret;
}

int bk7258_wdt_test_start(uint32_t timeout_ms)
{
  return (int)bk_wdt_start(timeout_ms);
}

int bk7258_wdt_test_feed(void)
{
  return (int)bk_wdt_feed();
}

int bk7258_wdt_test_setreason(uint32_t reason)
{
  bk_misc_set_reset_reason(reason);
  bk_misc_set_ap_reset_reason(reason);
  return (int)BK_OK;
}

void bk7258_wdt_test_force_reset(void)
{
  bk_wdt_force_reboot();
}

int bk7258_wdt_test_sensor(void)
{
  uint32_t temperature = 0;
  float stored_t = 0.0f;
  float stored_v = 0.0f;
  int t_ret;
  int v_ret;
  int r_ret;

  r_ret = temp_detect_get_temperature(&temperature);
  t_ret = (int)bk_sensor_get_current_temperature(&stored_t);
  v_ret = (int)bk_sensor_get_current_voltage(&stored_v);

  printf("wdttest: raw_temp=%lu (ret=%d) stored_temp=%d.%01d (ret=%d) "
         "stored_volt=%d.%01d (ret=%d) init=%d\n",
         (unsigned long)temperature, r_ret,
         (int)stored_t, (int)((stored_t - (int)stored_t) * 10.0f), t_ret,
         (int)stored_v, (int)((stored_v - (int)stored_v) * 10.0f), v_ret,
         (int)temp_detect_is_init());
  return r_ret;
}

/* end of probe hooks */
