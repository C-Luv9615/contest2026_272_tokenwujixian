/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_netheap.c
 *
 * The SRAM arena behind the Armino Wi-Fi driver's packet buffers, with lwIP's
 * mem.c API on top of it.
 *
 * Two consumers: compat/bk7258_netbuf.c, which builds every pbuf on a block
 * from netbuf_alloc() (it replaced lwIP's pbuf.c, memp.c, def.c, ip4_addr.c
 * and stats.c as well), and whatever Armino unit calls mem_malloc_trx() or
 * mem_free() from the still-compiled baseline. Both end up in the same arena.
 * lwIP's own mem.c is no longer compiled, so the CP pays for no ram_heap
 * (MEM_SIZE = 51,200 B plus headers) and no memp pools -- just this arena.
 *
 * Why the arena is not the NuttX kernel heap, which is where this file
 * started: the CP's kernel heap spans the 5 MiB PSRAM region, and its SRAM
 * fallback is small and quickly consumed by Armino's own os_malloc() calls.
 * A pbuf allocated from it can therefore land in PSRAM, and the MAC's TX DMA
 * cannot read PSRAM -- on the wire that is one `ac3_tx_dma_dead` per frame
 * and no EAPOL-4WAY completion, which is exactly what both the hand-written
 * pbuf and this mem.c replacement hit while they allocated from kmm. A static
 * arena pinned in .bss keeps pbuf memory in SRAM by construction, and its
 * size is a Kconfig number instead of a property of whatever else allocated
 * first.
 *
 * The symbol set below is mem.c's for this configuration, read off
 * lwip-2.1.2/src/core/mem.c with MEM_TRX_DYNAMIC_EN=1, MEM_LIBC_MALLOC=0,
 * MEM_USE_POOLS=0, MEMP_MEM_MALLOC=0:
 *
 *   mem_init()                              mem.c:593
 *   mem_trim(mem, size)                     mem.c:845
 *   mem_malloc_trx(size, type)              mem.c:991  (mem_malloc and
 *                                                     mem_malloc_rx are
 *                                                     macros over it in
 *                                                     lwip/mem.h)
 *   mem_calloc(count, size)                 mem.c:1210
 *   mem_free(mem)                           mem.c:758
 *   get_mem_size(mem)                       mem.c:697
 *   mem_sanity_check(mem)                   mem.c:735
 *
 * There is no mem_realloc in lwIP 2.1.2; mem_trim is its name for the same
 * operation, and pbuf_realloc() calls that.
 *
 * netbuf_alloc()/netbuf_free() are the arena itself, declared in
 * compat/bk7258_netbuf_arena.h; the mem.c names are thin wrappers over them.
 *
 * Compiled with lwIP's own flag set (chips/bk7258/wifi_armino/CMakeLists.txt)
 * so the signatures are the ones lwip/mem.h declares, and deliberately kept
 * out of the Armino allocator renaming: it calls no C allocator at all.
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/irq.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lwip/mem.h"
#include "bk7258_netbuf_arena.h"

#include <components/log.h>

/****************************************************************************
 * Arena
 ****************************************************************************/

#ifndef CONFIG_BK7258_NETBUF_ARENA_SIZE
#  error "CONFIG_BK7258_NETBUF_ARENA_SIZE is missing from chips/bk7258/Kconfig"
#endif

/* lwIP's MEM_ALIGNMENT is 4 (lwipopts.h), and pbuf_alloc() runs the payload
 * pointer through LWIP_MEM_ALIGN on top of that, so every block handed out
 * here is 4-byte aligned and 4-byte sized.
 */

#define NETBUF_ALIGN      4u
#define NETBUF_ALIGN_UP(n) \
  (((size_t)(n) + (NETBUF_ALIGN - 1u)) & ~((size_t)NETBUF_ALIGN - 1u))

/* Block header. `size` counts the header and the payload together and is
 * always a multiple of NETBUF_ALIGN, so the block that follows starts aligned
 * and two adjacent blocks can be merged by adding their sizes.
 *
 * `next_free` is only meaningful while the block is free, and the free list is
 * kept in address order -- that is what makes "is the next block adjacent?"
 * a subtraction instead of a search.
 */

struct netbuf_hdr
{
  size_t size;
  struct netbuf_hdr *next_free;
};

#define NETBUF_HDR_SIZE NETBUF_ALIGN_UP(sizeof(struct netbuf_hdr))

/* 8-byte aligned so the payload is 4-byte aligned even with an odd-sized
 * header, and so a DMA descriptor placed in a pbuf's headroom starts on a
 * word boundary. .bss puts it in the CP's SRAM region by construction.
 */

static uint8_t s_netbuf_arena[CONFIG_BK7258_NETBUF_ARENA_SIZE]
  __attribute__((aligned(8)));

static struct netbuf_hdr *s_free;
static bool s_logged;

/* One-shot diagnostic, for the board bring-up this arena exists for: it puts
 * the arena's address range and the first payload pointer in the log next to
 * the driver's own messages, so a reader can tell SRAM from PSRAM (0x28xx_xxxx
 * against 0x60xx_xxxx) without a debugger. Warning level because the Armino
 * log default level is INFO in a release build and a debug line would simply
 * not be printed.
 */

static void netbuf_log_first(void *first)
{
  if (s_logged)
    {
      return;
    }

  s_logged = true;
  BK_LOGW(NULL, "[netbuf] arena=%08x-%08x size=%u first=%08x\n",
          (unsigned int)(uintptr_t)s_netbuf_arena,
          (unsigned int)(uintptr_t)(s_netbuf_arena +
                                    sizeof(s_netbuf_arena) - 1u),
          (unsigned int)sizeof(s_netbuf_arena),
          (unsigned int)(uintptr_t)first);
}

/* The whole arena starts as one free block. Idempotent: mem_init() is called
 * from net_wlan_initial() in lwip_init()'s order, which may be after the
 * driver already allocated, so it must not reset a live arena.
 */

static void netbuf_init(void)
{
  if (s_free != NULL)
    {
      return;
    }

  s_free = (struct netbuf_hdr *)s_netbuf_arena;
  s_free->size = (size_t)sizeof(s_netbuf_arena) & ~(size_t)(NETBUF_ALIGN - 1u);
  s_free->next_free = NULL;
}

static bool netbuf_in_arena(const void *p)
{
  const uint8_t *u = (const uint8_t *)p;

  return u >= s_netbuf_arena + NETBUF_HDR_SIZE &&
         u < s_netbuf_arena + sizeof(s_netbuf_arena);
}

/****************************************************************************
 * Allocator: first fit, address-ordered free list, adjacent blocks merged
 ****************************************************************************/

void mem_init(void)
{
  netbuf_init();
}

/* The arena is shared by the wpa thread, the MAC's TX path and the NuttX
 * netdev lower half, so the free list needs mutual exclusion.  lwIP's mem.c had
 * mem_mutex for this; up_irq_save() is the same protection here and
 * is safe from interrupt context too, at the cost of masking interrupts for the
 * length of a first-fit walk (microseconds).  A mutex was rejected: this code
 * runs from contexts that must not block.
 */

void *netbuf_alloc(size_t size)
{
  struct netbuf_hdr *blk;
  struct netbuf_hdr *prev = NULL;
  irqstate_t flags;
  void *ret = NULL;
  size_t need;

  netbuf_init();
  flags = up_irq_save();
  need = NETBUF_ALIGN_UP(size);

  for (blk = s_free; blk != NULL; prev = blk, blk = blk->next_free)
    {
      if (blk->size < need + NETBUF_HDR_SIZE)
        {
          continue;
        }

      if (blk->size - (need + NETBUF_HDR_SIZE) >= NETBUF_HDR_SIZE + NETBUF_ALIGN)
        {
          /* Split: the tail keeps its own header. Handing out the whole block
           * instead would waste up to a frame per allocation, and there is no
           * allocator behind this one to give it back.
           */

          struct netbuf_hdr *rest =
            (struct netbuf_hdr *)((uint8_t *)blk + NETBUF_HDR_SIZE + need);

          rest->size = blk->size - (NETBUF_HDR_SIZE + need);
          rest->next_free = blk->next_free;

          if (prev == NULL)
            {
              s_free = rest;
            }
          else
            {
              prev->next_free = rest;
            }

          blk->size = NETBUF_HDR_SIZE + need;
        }
      else
        {
          /* Take the whole block; the unused tail stays inside the
           * allocation and comes back on netbuf_free().
           */

          if (prev == NULL)
            {
              s_free = blk->next_free;
            }
          else
            {
              prev->next_free = blk->next_free;
            }
        }

      blk->next_free = NULL;
      netbuf_log_first((uint8_t *)blk + NETBUF_HDR_SIZE);
      ret = (uint8_t *)blk + NETBUF_HDR_SIZE;
      break;
    }

  /* Out of arena. Upstream lwIP returns NULL here and the caller drops the
   * frame, which is the right answer for a bring-up: a dropped frame shows up
   * in the log, a panic does not.
   */

  up_irq_restore(flags);
  return ret;
}

void *mem_malloc_trx(mem_size_t size, u8_t type)
{
  /* `type` (MEM_TYPE_TX / MEM_TYPE_RX / MEM_TYPE_POOL) selected one of two
   * budgets inside mem.c's arena, which existed so one direction could not
   * starve the other out of a fixed heap. One arena, one budget here: the
   * TX/RX split comes back if the log ever shows one direction crowding the
   * other out.
   */

  (void)type;
  return netbuf_alloc(size);
}

void *mem_calloc(mem_size_t count, mem_size_t size)
{
  size_t alloc_size = (size_t)count * (size_t)size;
  void *p;

  /* Same overflow check as mem.c's: the product has to survive the round trip
   * through mem_size_t.
   */

  if ((size_t)(mem_size_t)alloc_size != alloc_size)
    {
      return NULL;
    }

  p = netbuf_alloc(alloc_size);
  if (p != NULL)
    {
      memset(p, 0, alloc_size);
    }

  return p;
}

void netbuf_free(void *mem)
{
  struct netbuf_hdr *blk;
  struct netbuf_hdr *prev = NULL;
  struct netbuf_hdr *cur;
  irqstate_t flags;

  if (mem == NULL || !netbuf_in_arena(mem))
    {
      /* Not one of ours. Dropping it keeps a stray pointer from corrupting
       * the free list. */

      return;
    }

  blk = (struct netbuf_hdr *)((uint8_t *)mem - NETBUF_HDR_SIZE);

  flags = up_irq_save();

  /* Address-ordered insert. */

  for (cur = s_free; cur != NULL && cur < blk; cur = cur->next_free)
    {
      prev = cur;
    }

  if (cur == blk)
    {
      /* Already free: a double free would make a cycle out of the list. */

      up_irq_restore(flags);
      return;
    }

  blk->next_free = cur;
  if (prev == NULL)
    {
      s_free = blk;
    }
  else
    {
      prev->next_free = blk;
    }

  /* Merge with the successor, then with the predecessor. Without the merges
   * the arena fragments into one block per frame ever allocated and the
   * largest frame that ever fits is the largest single gap left; with them,
   * a steady stream of allocate/free leaves one block again.
   */

  if (cur != NULL && (uint8_t *)blk + blk->size == (uint8_t *)cur)
    {
      blk->size += cur->size;
      blk->next_free = cur->next_free;
    }

  if (prev != NULL && (uint8_t *)prev + prev->size == (uint8_t *)blk)
    {
      prev->size += blk->size;
      prev->next_free = blk->next_free;
    }

  up_irq_restore(flags);
}

void mem_free(void *mem)
{
  netbuf_free(mem);
}

/* pbuf_realloc() asks for a smaller block and keeps whatever pointer comes
 * back. Nothing is resized here: the block keeps its size, and the bytes
 * above p->tot_len simply stay unused for the pbuf's lifetime. lwIP's own
 * caller tolerates that (it only needs the pointer and writes the length
 * fields itself), and the cost is bounded -- at most the difference between
 * what a pbuf was allocated with and what it ended up carrying, per live
 * pbuf, not a growing leak. Splitting a trimmed block in place would be the
 * change to make if the arena ever runs short on the frame-size distribution
 * the board actually sees.
 */

void *mem_trim(void *mem, mem_size_t size)
{
  (void)size;
  return mem;
}

/****************************************************************************
 * Introspection
 *
 * With in-band headers both questions have real answers again, so they are
 * answered rather than stubbed: get_mem_size() is what pbuf_length_get()
 * asks for a PBUF_ROM/PBUF_REF pbuf's allocation size, and mem_sanity_check()
 * is Armino's "is this pointer one of our buffers" probe.
 ****************************************************************************/

u32_t get_mem_size(void *rmem)
{
  struct netbuf_hdr *blk;

  if (rmem == NULL || !netbuf_in_arena(rmem))
    {
      return 0;
    }

  blk = (struct netbuf_hdr *)((uint8_t *)rmem - NETBUF_HDR_SIZE);
  return (u32_t)(blk->size - NETBUF_HDR_SIZE);
}

uint32_t mem_sanity_check(void *mem)
{
  if (mem == NULL || !netbuf_in_arena(mem))
    {
      return 0;
    }

  return (((uintptr_t)mem & (NETBUF_ALIGN - 1u)) == 0) ? 1u : 0u;
}
