/****************************************************************************
 * chips/bk7258/bk7258_stack_base.c
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
 * Armino stack-base helpers
 *
 * The three symbols of middleware/arch/cm33/stack_base.c:
 *
 *   code_addr_is_valid()        is a word a plausible return address?
 *   stack_mem_dump()            hexdump a stack window
 *   arch_parse_stack_backtrace() walk a stack and print the PCs it holds
 *
 * None of them is referenced by the current image; they are the reading half
 * of the exception dump, and their writer half (arch_is_enter_exception,
 * bk_system_dump, the dump hooks) is still in trap_base.c.  Bodies are the
 * authority's, including the CONFIG_DEBUG_VERSION / CONFIG_DUMP_ENABLE branch
 * that feeds the watchdog while a dump is being printed -- that branch is
 * compiled out today, and the port keeps it anyway.
 *
 * code_addr_is_valid()'s rule is "odd address in flash text, ITCM or IRAM";
 * the authority has a commented-out stricter version that also disassembles
 * the preceding instruction, left as it is.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <common/bk_assert.h>
#include <driver/flash_partition.h>
#include <os/mem.h>
#include <soc/bk7258/reg_base.h>

#include "arch_interrupt.h"
#include "bk_wdt.h"
#include "stack_base.h"
#include "wdt_driver.h"

#define STACK_CALLBACK_BUF_SIZE 32
#define SOC_ITCM_CODE_SIZE      (0x4000)

extern unsigned char __iram_start__;
extern unsigned char __iram_end__;
extern char __etext;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline int addr_is_in_flash_txt(uint32_t addr)
{
  return (addr > (uint32_t)(SOC_FLASH_DATA_BASE +
                            CONFIG_APPLICATION_PARTITION_OFFSET) &&
          addr < (uint32_t)&__etext);
}

static inline int addr_is_in_itcm_txt(uint32_t addr)
{
  return (addr > (uint32_t)SOC_ITCM_DATA_BASE &&
          addr < (uint32_t)(SOC_ITCM_DATA_BASE + SOC_ITCM_CODE_SIZE));
}

static inline int addr_is_in_iram_txt(uint32_t addr)
{
  if (addr > (uint32_t)&__iram_start__ && addr < (uint32_t)&__iram_end__)
    {
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int code_addr_is_valid(uint32_t addr)
{
  if (addr % 2 == 0)
    {
      return false;
    }

  return (addr_is_in_flash_txt(addr) ||
          addr_is_in_itcm_txt(addr) ||
          addr_is_in_iram_txt(addr));
}

void stack_mem_dump(uint32_t stack_top, uint32_t stack_bottom)
{
  unsigned char *data = NULL;
  volatile uint32_t value = 0;
  uint32_t cnt = 0;
  uint32_t sp = stack_top;
  uint32_t fp = stack_bottom;

  if (stack_bottom <= stack_top)
    {
      return;
    }

  BK_DUMP_OUT(">>>>stack mem dump begin, stack_top=%08x, stack end=%08x\r\n",
              stack_top, stack_bottom);

  for (; sp < fp; sp += sizeof(size_t))
    {
      value = os_get_word(sp);
      data = ((unsigned char *)&value);

      if ((cnt++ & 0x7) == 0)
        {
          BK_DUMP_OUT("\r\n");
        }

#if CONFIG_DEBUG_VERSION || CONFIG_DUMP_ENABLE
      if ((cnt & 0xff) == 0)
        {
#if (CONFIG_TASK_WDT)
          bk_task_wdt_feed();
#endif
          if (arch_is_enter_exception())
            {
              bk_wdt_force_feed();
            }
        }
#endif

      BK_DUMP_OUT("%02x %02x %02x %02x ", data[0], data[1], data[2], data[3]);
    }

  BK_DUMP_OUT("\r\n");
  BK_DUMP_OUT("<<<<stack mem dump end. stack_top=%08x, stack end=%08x\r\n",
              stack_top, stack_bottom);
  BK_DUMP_OUT("\r\n");
}

/* The stack grows from bottom to top:
 *
 *   |   | <- minimum_addr = (bottom - stack_size)
 *   |   |
 *   |   | <- top
 *   | . |
 *   | . |
 *   | . | <- bottom, (big address)
 */

void arch_parse_stack_backtrace(const char *str_type, uint32_t stack_top,
                                uint32_t stack_bottom, uint32_t stack_size,
                                bool thumb_mode)
{
  uint32_t call_stack_buf[STACK_CALLBACK_BUF_SIZE] = {0};
  uint32_t stack_minimum = stack_bottom - stack_size;
  uint32_t pc;
  int call_stack_index = 0;
  uint32_t init_stack_top = stack_top;

  for (; stack_top < stack_bottom &&
         call_stack_index < STACK_CALLBACK_BUF_SIZE;
       stack_top += sizeof(size_t))
    {
      pc = *((uint32_t *)stack_top);

      if (code_addr_is_valid(pc))
        {
          if (pc & 1)
            {
              pc = pc - 1;
            }

          call_stack_buf[call_stack_index] = pc;
          call_stack_index++;
        }
    }

  if (call_stack_index > 0)
    {
      int index;

      BK_DUMP_OUT("%-16s   [0x%-6x ~ 0x%-6x]   0x%-6x   %-4d   %-8d",
                  str_type, stack_minimum, stack_bottom, init_stack_top,
                  stack_size, init_stack_top < stack_minimum);

      for (index = 0; index < call_stack_index; index++)
        {
          BK_DUMP_OUT("%lx ", call_stack_buf[index]);
        }

      BK_DUMP_OUT("\r\n");
    }
  else if (init_stack_top < stack_minimum)
    {
      BK_DUMP_OUT("%-16s   [0x%-6x ~ 0x%-6x]   0x%-6x   %-4d   %-8d   ",
                  str_type, stack_minimum, stack_bottom, init_stack_top,
                  stack_size, init_stack_top < stack_minimum);
    }
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
