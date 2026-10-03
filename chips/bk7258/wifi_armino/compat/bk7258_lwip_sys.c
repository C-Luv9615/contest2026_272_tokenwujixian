/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_lwip_sys.c
 *
 * The lwip OS calls of the kept lwip buffer units (pbuf.c, mem.c, memp.c),
 * on NuttX (docs/compose/spec/bk7258-wifi-native.md, S2.7). The baseline
 * port, lwip-2.1.2/port/sys_arch.c, also carries lwip's threads, mboxes and
 * per-thread semaphores for the excluded protocol stack, and does not build
 * against NuttX's pthread types; these are the five the link needs, with
 * the baseline's semantics:
 *
 *  - sys_arch_protect(): one global mutex, as the baseline's
 *    CFG_ENABLE_LWIP_MUTEX (sys_arch.c:55, 463-485). Task context only,
 *    as there (rtos_lock_mutex() asserts it). Recursive here, which lwip's
 *    contract asks for and the baseline's FreeRTOS mutex was not.
 *  - sys_mutex_*(): mem.c's heap lock (sys_arch.c:368-404), without the
 *    SYS_STATS counters.
 *
 * Compiled with NuttX's flags; the lwip types are restated from
 * port/arch/cc.h (sys_prot_t int), port/arch/sys_arch.h (sys_mutex_t is
 * beken_mutex_t, void *) and lwip/err.h (err_t s8_t).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

typedef int sys_prot_t;
typedef void *sys_mutex_t;
typedef int8_t err_t;

#define ERR_OK   0
#define ERR_VAL  (-6)

sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t pval);
err_t sys_mutex_new(sys_mutex_t *mutex);
void sys_mutex_lock(sys_mutex_t *mutex);
void sys_mutex_unlock(sys_mutex_t *mutex);

/* See bk7258_lwip_compat.h. */

uint32_t pbuf_debug_flag;

static rmutex_t g_lwip_protect = NXRMUTEX_INITIALIZER;

sys_prot_t sys_arch_protect(void)
{
  nxrmutex_lock(&g_lwip_protect);
  return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
  (void)pval;
  nxrmutex_unlock(&g_lwip_protect);
}

err_t sys_mutex_new(sys_mutex_t *mutex)
{
  mutex_t *m = kmm_malloc(sizeof(*m));

  if (m == NULL)
    {
      return ERR_VAL;
    }

  nxmutex_init(m);
  *mutex = m;
  return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
  nxmutex_lock(*mutex);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
  nxmutex_unlock(*mutex);
}
