/****************************************************************************
 * chips/bk7258/include/reset_reason.h
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

#ifndef __CHIPS_BK7258_INCLUDE_RESET_REASON_H
#define __CHIPS_BK7258_INCLUDE_RESET_REASON_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "sdkconfig.h"

#include <soc/soc.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Addresses that are load-bearing across a chip reset: the AON PMU survives
 * it, so what is written here is what the ROM/BL2 reads to classify the next
 * boot.  Same values as
 * cp/middleware/driver/reset_reason/reset_reason.h:25-67.
 */

#if (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX)
#  define START_TYPE_ADDR      (SOC_AON_PMU_REG_BASE + (3 << 2))

/* REBOOT_TAG_ADDR: CPU0-APP sets the reset tag before an NMI watchdog reboot.
 * A set tag makes the BootROM warm fast-jump into the running app; a cleared
 * tag runs the full cold chain (BootROM -> BL2).
 */

#  define REBOOT_TAG_ADDR      (0x20003FF8 + SOC_ADDR_OFFSET)
#  define REBOOT_TAG_REQ       (0xAA55AA55)
#  define PERSIST_MEMORY_ADDR  (0x20003FFC + SOC_ADDR_OFFSET)
#elif (CONFIG_SOC_BK7286XX)
#  define START_TYPE_ADDR      (SOC_AON_PMU_REG_BASE + (3 << 2))
#  define REBOOT_TAG_ADDR      (0x0)
#  define REBOOT_TAG_REQ       (0xAA55AA55)
#  define PERSIST_MEMORY_ADDR  (0x0)
#else
#  define START_TYPE_ADDR      (0x0080a080)
#  define PERSIST_MEMORY_ADDR  (0x0040001c)
#endif

#define CRASH_ILLEGAL_JUMP_VALUE     0xbedead00
#define CRASH_UNDEFINED_VALUE        0xbedead01
#define CRASH_PREFETCH_ABORT_VALUE   0xbedead02
#define CRASH_DATA_ABORT_VALUE       0xbedead03
#define CRASH_UNUSED_VALUE           0xbedead04

#define POWERON_INIT_MEM_TAG         (0xaaaaaaaa)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void show_reset_reason(void);
uint32_t reset_reason_init(void);

/* Warm-boot tag, written by the chip reset path. */

void set_reboot_tag(uint32_t tag);
uint32_t get_reboot_tag(void);
void set_nmi_vector(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_RESET_REASON_H */
