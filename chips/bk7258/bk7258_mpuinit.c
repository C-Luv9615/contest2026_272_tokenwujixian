/****************************************************************************
 * chips/bk7258/bk7258_mpuinit.c
 *
 * ARMv8-M (Cortex-M33) MPU bring-up for BK7258.
 *
 * The BK7258 has an MPU, not an MMU (ARMv8-M MPU with unified instruction
 * and data regions).  This file programs the kernel region table and enables
 * the MPU.  It runs on every physical core (CPU0 CP __start, CPU1 and CPU2
 * AP __start paths) once .data/.bss are valid and the early fault handlers
 * are attached, so any MPU setup mistake produces a MemManage fault dump
 * instead of a silent hang.
 *
 * The policy follows Armino's mpu_cfg.c with CONFIG_CACHE_ENABLE=n, which
 * is how the imported baseline (and the closed Wi-Fi libraries) are built:
 * nothing in this port does D-cache maintenance around DMA, so only memory
 * that no DMA engine and no other core ever writes may be cached.
 *
 * The table covers every address from 0 up to the PPB (0xe0000000), as
 * Armino's does.  With PRIVDEFENA a gap falls back to the default memory
 * map, which is cacheable for 0x00000000..0x3fffffff and 0x60000000..
 * 0x9fffffff, so a gap would silently cache an alias or a TCM.
 *
 *   1. Flash XIP (0x02000000) is Normal write-through, read-only.  It is the
 *      only cached window.  Flash is written through the controller and read
 *      back through the controller (bk7258_flash_read, Armino's
 *      flash_read_common), never through XIP, so a write cannot leave stale
 *      lines that anyone reads.
 *   2. On-chip SRAM (0x28000000) is Normal non-cacheable, inner shareable.
 *      It holds RPMSG_SHM, SWAP, PWR_MNG, the AP spinlock and the CP heap
 *      the MAC/PHY DMA writes Wi-Fi pbufs into.  The region runs up to
 *      0x40000000 so the 0x38000000 non-secure alias is covered too, as in
 *      Armino, instead of falling back to the cacheable default map.
 *   3. Peripheral space (0x40000000) is Device nGnRnE, execute-never --
 *      Armino's MAIR 0x00 (its built mpu_regions), not the nGnRE (early
 *      write acknowledgement) NuttX calls "Device".
 *   4. PSRAM (0x60000000) is Normal non-cacheable.  Both heaps reach it
 *      (kumm_addregion is not type-aware), so audio DMA buffers can land
 *      there.
 *   5. Everything else Armino maps -- ITCM (0x00000000), the SRAM
 *      instruction/non-secure aliases (0x08000000, 0x18000000), DTCM
 *      (0x20000000) and the QSPI windows (0x64000000) -- is Normal
 *      non-cacheable; the rest up to the PPB is Device nGnRnE,
 *      execute-never.
 *
 * ponytail: phase one.  Caching the AP's private SRAM and PSRAM needs D-cache
 * maintenance in every AP DMA user first; see the MPU notes in the T1 log.
 *
 * PRIVDEFENA stays on so the PPB (0xe0000000+, which the MPU never covers)
 * and anything above remain reachable in this flat privileged build.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/barriers.h>

#include "arm_internal.h"
#include "mpu.h"

#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"

#ifdef CONFIG_ARM_MPU

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct mpu_region_s g_bk7258_mpu_regions[] =
{
  /* ITCM and whatever else sits below flash (0x00000000, 32 MiB).  Nothing
   * in this port runs from ITCM (.itcm links at flash addresses), but
   * Armino maps it non-cacheable and the default map would cache it. */

  {
    UINT32_C(0x00000000),           BK7258_FLASH_BASE,
    MPU_RBAR_AP_RWNO,               MPU_RLAR_NONCACHEABLE
  },

  /* Flash XIP alias window (0x02000000, 32 MiB).  Both the CP image
   * (0x02010000) and the AP image (0x02160000) execute from here.  Normal
   * write-through (Armino: WT, read-allocate), read-only, privileged. */

  {
    BK7258_FLASH_BASE,              UINT32_C(0x02000000),
    MPU_RBAR_AP_RONO,               MPU_RLAR_WRITE_THROUGH
  },

  /* Between flash and SRAM: the SRAM instruction alias (0x08000000) and
   * its non-secure twin (0x18000000), DTCM (0x20000000).  The 0x08000000
   * alias is the same 640 KiB the DMA writes, so it must not be cached
   * either.  Normal non-cacheable. */

  {
    BK7258_FLASH_BASE + UINT32_C(0x02000000),
    BK7258_SRAM_CAPACITY_BASE - (BK7258_FLASH_BASE + UINT32_C(0x02000000)),
    MPU_RBAR_AP_RWNO,               MPU_RLAR_NONCACHEABLE
  },

  /* On-chip SRAM: the 640 KiB window at 0x28000000 and its 0x38000000
   * alias, up to the peripheral space.  Normal non-cacheable, inner
   * shareable, privileged. */

  {
    BK7258_SRAM_CAPACITY_BASE,
    UINT32_C(0x40000000) - BK7258_SRAM_CAPACITY_BASE,
    MPU_RBAR_AP_RWNO | MPU_RBAR_SH_INNER,
    MPU_RLAR_NONCACHEABLE
  },

  /* Peripheral address space (0x40000000, 512 MiB): SYS, AON, UART, TIMER,
   * Mailbox, GDMA, SADC, TRNG, OTP, PSRAM controller, ...  Device nGnRnE,
   * privileged, never cached.  Execute-never on top of Device: no code ever
   * lives in this window, so an XN violation (IACCVIOL) is a hard
   * out-of-band memory-protection signal for the bk7258_mpu_test probe. */

  {
    UINT32_C(0x40000000),           UINT32_C(0x20000000),
    MPU_RBAR_AP_RWNO | MPU_RBAR_XN, MPU_RLAR_STRONGLY_ORDER
  },

  /* External PSRAM window (0x60000000, 64 MiB).  The controller maps up to
   * 64 MiB; the fitted-device size is discovered before any portion enters
   * the heap.  Normal non-cacheable, privileged. */

  {
    BK7258_PSRAM_BASE,              BK7258_PSRAM_WINDOW_SIZE,
    MPU_RBAR_AP_RWNO,               MPU_RLAR_NONCACHEABLE
  },

  /* QSPI0/QSPI1 memory-mapped windows (0x64000000, 128 MiB).  Unused here;
   * Armino maps them execute-never.  Normal non-cacheable. */

  {
    BK7258_PSRAM_BASE + BK7258_PSRAM_WINDOW_SIZE, UINT32_C(0x08000000),
    MPU_RBAR_AP_RWNO | MPU_RBAR_XN, MPU_RLAR_NONCACHEABLE
  },

  /* The rest up to the PPB (0x6c000000..0xdfffffff).  Armino: Device
   * nGnRnE, execute-never. */

  {
    UINT32_C(0x6c000000),           UINT32_C(0xe0000000) - UINT32_C(0x6c000000),
    MPU_RBAR_AP_RWNO | MPU_RBAR_XN, MPU_RLAR_STRONGLY_ORDER
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Reset the MPU to disabled and invalidate every region the hardware
 * implements.  The Bootloader and the vendor AP lifecycle may have left the
 * MPU enabled with opaque regions over shared SRAM; starting clean makes the
 * table below deterministic on every core.  This mirrors mpu_reset_internal()
 * but is kept here so the chip bring-up never depends on CONFIG_ARM_MPU_RESET
 * being enabled by the board config. */

static void bk7258_mpu_reset(void)
{
  uint32_t region;
  uint32_t regions;

  regions = (getreg32(MPU_TYPE) & MPU_TYPE_DREGION_MASK) >>
            MPU_TYPE_DREGION_SHIFT;

  for (region = 0; region < regions; region++)
    {
      putreg32(region, MPU_RNR);
      putreg32(0, MPU_RLAR);
      putreg32(0, MPU_RBAR);
    }

  putreg32(0, MPU_CTRL);

  UP_MB();
}

/* Program the region table with fixed region numbers 0..n.  Direct register
 * writes are used instead of mpu_configure_region()/mpu_modify_region() so a
 * secondary SMP core does not advance the shared g_mpu_region allocator:
 * every core ends up with the identical config at regions 0..n.  The table
 * is clamped to the number of data regions the integrated MPU actually
 * implements (MPU_TYPE.DREGION). */

static void bk7258_mpu_configure(void)
{
  const struct mpu_region_s *conf;
  size_t count;
  size_t index;
  uint32_t regions;
  uintptr_t limit;
  uintptr_t rbase;

  regions = (getreg32(MPU_TYPE) & MPU_TYPE_DREGION_MASK) >>
            MPU_TYPE_DREGION_SHIFT;

  count = sizeof(g_bk7258_mpu_regions) / sizeof(g_bk7258_mpu_regions[0]);
  if (count > regions)
    {
      count = regions;
    }

  for (index = 0; index < count; index++)
    {
      conf = &g_bk7258_mpu_regions[index];

      /* ARMv8-M regions only need 32-byte granularity: RBAR.ADDR and
       * RLAR.LIMIT mask to bit 5, and every base and size above is
       * 32-byte aligned.  Regions must not overlap. */

      rbase = conf->base & MPU_RBAR_BASE_MASK;
      limit = (conf->base + conf->size - 1) & MPU_RLAR_LIMIT_MASK;

      putreg32(index, MPU_RNR);
      putreg32(rbase | conf->flags1, MPU_RBAR);
      putreg32(limit | conf->flags2 | MPU_RLAR_ENABLE, MPU_RLAR);

      UP_MB();
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_mpuinit(void)
{
  bk7258_mpu_reset();
  bk7258_mpu_configure();

  /* mpu_control() programs MAIR0/MAIR1 (the attribute encodings the RLAR
   * indexes reference) and enables the MPU with PRIVDEFENA so uncommon
   * addresses keep working under the privileged default map. */

  mpu_control(true, false, true);
}

#endif /* CONFIG_ARM_MPU */