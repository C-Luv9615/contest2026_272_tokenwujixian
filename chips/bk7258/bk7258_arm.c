/****************************************************************************
 * chips/bk7258/bk7258_arm.c
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
 * Armino CM33 core helpers
 *
 * The nine symbols of middleware/arch/cm33/arm.c.  arch_init() and
 * smem_reset_lastblock() are empty in the authority and stay empty here: they
 * are hooks the Armino startup code calls, and this port's startup
 * (bk7258_start.c) does not.  Both are linked anyway, because trap_base.c
 * references arch_init.
 *
 * The rest is the core face:
 *
 *   arch_sleep / arch_deep_sleep   WFI with the SLEEPDEEP bit cleared/set, in
 *                                  the .iram / .itcm_sec_code sections the
 *                                  authority puts them in
 *   check_IRQ_pending              every pending NVIC line as a 64-bit mask
 *   arch_atomic_{set,set_return,clear}, arch_fence
 *                                  the LDREX/STREX and DSB/ISB primitives the
 *                                  Armino spinlocks and the Wi-Fi/PHY code
 *                                  build on
 *
 * The section attributes are reproduced even though .itcm_sec_code has no
 * ITCM region to live in here -- the authority builds the same way, and
 * dropping an attribute would change where the linker is allowed to put the
 * function.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include "armstar.h"
#include "bk_arch.h"

void arch_init(void)
{
}

void smem_reset_lastblock(void)
{
}

__attribute__((section(".itcm_sec_code"))) void arch_deep_sleep(void)
{
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
  __asm volatile ("dsb" ::: "memory");
  __WFI();
  __asm volatile ("isb");
}

__attribute__((section(".iram"))) void arch_sleep(void)
{
  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
  __asm volatile ("dsb" ::: "memory");
  __WFI();
  __asm volatile ("isb");
}

__attribute__((section(".itcm_sec_code"))) uint64_t check_IRQ_pending(void)
{
  return NVIC_GetAllPendingIRQ();
}

void arch_atomic_set(volatile uint32_t *lk)
{
  int result;

  do
    {
      while (__LDAEX(lk) != 0)
        {
        }

      result = __STREXW(1, lk);
    }
  while (result != 0);
}

int arch_atomic_set_return(volatile uint32_t *lk, int v)
{
  int result;

  if (__LDAEX(lk) != 0)
    {
      return 0;
    }
  else
    {
      result = __STREXW(v, lk);
      if (result != v)
        {
          return 0;
        }
      else
        {
          return 1;
        }
    }
}

void arch_atomic_clear(volatile uint32_t *lk)
{
  __STL(0, lk);
}

void arch_fence(void)
{
  __asm volatile
    (
      "	dsb 											\n"
      "	isb 											\n"
    );
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
