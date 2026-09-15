/*
 * chips/bk7258/wifi/glue/os_shim.c
 *
 * NuttX implementation of the Armino os_* memory callbacks. Backed by the
 * kernel heap (kmm_*); os_malloc/os_free/os_zalloc resolve here via the
 * os/mem.h macros.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "os/mem.h"
#include "os/str.h"

/* The vendor table takes the function address; do not expand the libc alias
 * from os/str.h at this definition site. */
#ifdef os_snprintf
#  undef os_snprintf
#endif

void *os_malloc_debug(const char *func_name, int line, size_t size,
                      int need_zero)
{
  (void)func_name;
  (void)line;
  return need_zero ? kmm_zalloc(size) : kmm_malloc(size);
}

void os_free_debug(const char *func_name, int line, void *pv)
{
  (void)func_name;
  (void)line;
  kmm_free(pv);
}

void *os_malloc_wifi_buffer(size_t size)
{
  /* Skeleton: no DMA-alignment contract yet; plain kernel heap. Revisit once
   * the vendor DMA/cache ABI is confirmed (plan §11.2). */
  return kmm_malloc(size);
}

void *os_sram_zalloc(size_t size)
{
  /* Armino distinguishes SRAM from PSRAM allocations; this port has a single
   * kernel heap, so the distinction collapses onto kmm_zalloc(). That is the
   * same backing store bk_prelude.h reaches when it maps os_sram_zalloc onto
   * os_zalloc() -> os_malloc_debug(..., need_zero=1) above.
   *
   * A real function is required in addition to that macro because
   * glue/vendor_sources/skbuff.c cannot include bk_prelude.h: the prelude
   * installs NuttX's one-argument spin_lock_irqsave() and skbuff.c uses the
   * Linux-style two-argument form. So skbuff.c:108,135,140 emit an external
   * call and resolve here, while prelude-carrying units keep using the macro.
   * Both paths end up in kmm_zalloc(), so the allocator stays single-sourced.
   */

  return kmm_zalloc(size);
}

void *os_memmove(void *dest, const void *src, size_t n)
{
  return memmove(dest, src, n);
}

/****************************************************************************
 * String formatting
 *
 * A real function rather than a macro over vsnprintf(), because
 * bk_wifi_adapter.c:1445 stores its address in the vendor value table
 * (`._os_snprintf = os_snprintf`) -- a macro has no address.
 ****************************************************************************/

INT32 os_snprintf(char *buf, UINT32 size, const char *fmt, ...)
{
  va_list ap;
  int ret;

  va_start(ap, fmt);
  ret = vsnprintf(buf, (size_t)size, fmt, ap);
  va_end(ap);

  return (INT32)ret;
}
