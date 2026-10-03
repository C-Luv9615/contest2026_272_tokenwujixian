/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_irq.c
 *
 * Armino interrupt API on NuttX's interrupt controller (docs/compose/spec/
 * bk7258-wifi-native.md, S2.7). The NVIC belongs to NuttX, so three Armino
 * units are excluded and replaced here:
 *
 *  - cp/middleware/driver/icu/interrupt_base.c   bk_int_* API
 *  - cp/middleware/arch/cm33/arch_interrupt.c    its own NVIC driver; its
 *    arch_int_init_all_irq() disables all 64 NVIC lines and PRIMASK
 *  - cp/middleware/driver/bk7258/interrupt.c     the 63 vector handlers, only
 *    reachable from the excluded Armino vector table
 *
 * Mapping, from the baseline: ICU_DEV_MAP (soc/bk7258/soc/icu_map.h) maps
 * every source to the NVIC line of the same number (all 64 rows, checked
 * against the baseline DWARF), so source n is NuttX IRQ NVIC_IRQ_FIRST + n.
 * Priority is IQR_PRI_DEFAULT (6) for every source except INT_SRC_LCD (0);
 * the core implements 3 priority bits (__NVIC_PRIO_BITS), so the register
 * value is prio << 5.
 *
 * Registration only enables the NVIC line, as arch_interrupt_register_int()
 * does; the SYS-level CPU0 interrupt enable stays with the Armino drivers
 * (for example dma_driver.c:107), as in the baseline.
 *
 * Compiled with NuttX's flags, so the Armino types are restated here:
 * icu_int_src_t is a 1-byte enum, int_group_isr_t is void (*)(void)
 * (include/driver/hal/hal_int_types.h:23).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "nvic.h"

#define ARMINO_INT_SRC_NONE       64      /* INT_SRC_NONE */
#define ARMINO_INT_SRC_LCD        27      /* the one priority-0 source */
#define ARMINO_IQR_PRI_DEFAULT    6       /* icu_reg.h:128 */
#define ARMINO_PRIO_SHIFT         5       /* 8 - __NVIC_PRIO_BITS */

#define BK_OK                     0
#define BK_FAIL                   (-1)
#define BK_ERR_INT_DEVICE_NONE    (-0x2500 - 1) /* int_types.h:24 */

typedef void (*int_group_isr_t)(void);

static int_group_isr_t g_armino_isr[ARMINO_INT_SRC_NONE];
static int_group_isr_t g_armino_logged[ARMINO_INT_SRC_NONE];

static int armino_irq_dispatch(int irq, void *context, void *arg)
{
  int_group_isr_t isr = g_armino_isr[(uintptr_t)arg];

  (void)irq;
  (void)context;

  if (isr != NULL)
    {
      isr();
    }

  return OK;
}

static bool armino_nvic_enabled(uint8_t src)
{
  uintptr_t reg = NVIC_IRQ0_31_ENABLE + ((src >> 5) << 2);

  return (getreg32(reg) & (UINT32_C(1) << (src & 31))) != 0;
}

static void armino_set_priority(uint8_t src, uint32_t prio)
{
  up_prioritize_irq(NVIC_IRQ_FIRST + src, (int)(prio << ARMINO_PRIO_SHIFT));
}

int bk_int_isr_register(uint8_t src, int_group_isr_t isr, void *arg)
{
  int irq = NVIC_IRQ_FIRST + src;
  irqstate_t flags;

  (void)arg; /* dropped by the baseline as well (interrupt_base.c:44) */

  if (src >= ARMINO_INT_SRC_NONE)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  /* A line NuttX already enabled for one of its own drivers must not be
   * taken over silently: whichever attached last would own it.
   */

  if (g_armino_isr[src] == NULL && armino_nvic_enabled(src))
    {
      syslog(LOG_ERR, "[ARMINO] irq src=%u already owned by NuttX, "
             "registration refused\n", src);
      return BK_FAIL;
    }

  /* Same order as the baseline: unregister, register, set priority. */

  up_disable_irq(irq);
  flags = enter_critical_section();
  g_armino_isr[src] = isr;
  leave_critical_section(flags);

  if (isr == NULL)
    {
      return BK_OK;
    }

  irq_attach(irq, armino_irq_dispatch, (void *)(uintptr_t)src);
  up_enable_irq(irq);
  armino_set_priority(src, src == ARMINO_INT_SRC_LCD ?
                           0 : ARMINO_IQR_PRI_DEFAULT);

  /* Logged once per source and handler. bk_adc_init()/bk_adc_deinit()
   * register and unregister INT_SRC_SARADC around every sample
   * (adc_driver.c), about once a second from temperature and voltage
   * detection; the baseline does the same and its bk_int_isr_register()
   * (interrupt_base.c:37) logs nothing.
   */

  if (g_armino_logged[src] != isr)
    {
      g_armino_logged[src] = isr;
      syslog(LOG_INFO, "[ARMINO] irq src=%u -> NuttX irq %d\n", src, irq);
    }

  return BK_OK;
}

int bk_int_isr_unregister(uint8_t src)
{
  int irq = NVIC_IRQ_FIRST + src;
  irqstate_t flags;

  if (src >= ARMINO_INT_SRC_NONE)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  up_disable_irq(irq);
  flags = enter_critical_section();
  g_armino_isr[src] = NULL;
  leave_critical_section(flags);
  irq_detach(irq);
  return BK_OK;
}

int bk_int_set_priority(uint8_t src, uint32_t prio)
{
  armino_set_priority(src, prio);
  return BK_OK;
}

/* interrupt_base.c:52 -> soc_isr_init() -> arch_isr_entry_init() ->
 * arch_int_init_all_irq(): disables PRIMASK and every NVIC line, which on
 * NuttX would switch off the console, the system tick and the mailbox.
 * NuttX has already initialised its interrupt controller, so nothing to do.
 */

void interrupt_init(void)
{
}

void interrupt_deinit(void)
{
}
