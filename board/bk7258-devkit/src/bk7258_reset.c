/****************************************************************************
 * board/bk7258-devkit/src/bk7258_reset.c
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <sys/boardctl.h>

/* Chip-level reset for BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER.
 *
 * The generic SYSRESETREQ path below only resets the CPU0 core: on this SoC
 * the warm reset vector re-enters the startup trampoline directly, so the
 * BootROM/BL2 chain never runs and the serial download handshake window
 * never opens.  "reboot bootloader" must reach the BL2 download listen
 * window instead, so it takes the vendor WDT path: a real chip-level reset
 * from the Always-On PMU, equivalent to the reset button.
 *
 * Every register fact below is lifted from the vendor Armino SDK (BK7258,
 * secure address map, SOC_ADDR_OFFSET == 0):
 *
 * - soc/bk7258/reg_base.h:
 *     SOC_AON_PMU_REG_BASE 0x44000000, SOC_AON_WDT_REG_BASE 0x44000600
 * - soc/bk7258_ap/soc/aon_pmu_reg.h:
 *     AON_PMU_R2_ADDR  = PMU base + 0x2 << 2
 *     AON_PMU_R41_ADDR = PMU base + 0x41 << 2
 * - driver/reset_reason/reset_reason.h (BK7236XX):
 *     REBOOT_TAG_ADDR 0x20003FF8 (free DTCM slot, REBOOT_TAG_REQ
 *     0xAA55AA55).  A set tag tells the BootROM to warm fast-jump into the
 *     previous app; a cleared tag makes any reset run the full cold chain
 *     (BootROM -> BL2).  The vendor's own planned reboots set the tag
 *     before arming the WDT (wdt_hal_nmi_reboot); deliberately clearing it
 *     turns the same WDT reset into the recover-from-hung-firmware cold
 *     path that the BootROM already implements.
 * - driver/reset_reason/reset_reason.c + include/components/system.h:
 *     RESET_SOURCE_WATCHDOG == 0x2.  The AON PMU survives the chip reset,
 *     so the reason recorded there is what the ROM/BL2 reads to classify
 *     the next boot: CP reason in PMU R0 bits[4:11], AP reason in
 *     bits[24:30].  Recording WATCHDOG here is what makes the boot cold.
 * - pmu/aon_pmu_driver.c + soc/bk7258_ap/hal/aon_pmu_hal.c:
 *     aon_pmu_drv_wdt_change_not_rosc_clk(): PMU R41 bits[1:0] = 0, selects
 *     clk_DIVD as the WDT LPO source instead of ROSC.
 *     aon_pmu_hal_wdt_rst_dev_enable(): PMU R2 bits[5:0] = 0x26, allows the
 *     AON WDT to reset the device at the PMU level.
 * - soc/common/hal/wdt_hal.c wdt_hal_force_reboot():
 *     arm the AON WDT with the mandatory two-write key sequence
 *     0x5A000A / 0xA5000A (key 0x5A then 0xA5 in bits[23:16], emergency
 *     period 0xA).
 *
 * The CP firmware runs from the 0x280xxxxx SRAM window, so the DTCM tag
 * slot at 0x20003FF8 is vendor-reserved and safe to write.
 */

#define BK7258_AON_PMU_BASE      0x44000000ul
#define BK7258_AON_PMU_R0_ADDR   (BK7258_AON_PMU_BASE + (0x0 << 2))
#define BK7258_AON_PMU_R2_ADDR   (BK7258_AON_PMU_BASE + (0x2 << 2))
#define BK7258_AON_PMU_R41_ADDR  (BK7258_AON_PMU_BASE + (0x41 << 2))
#define BK7258_AON_WDT_BASE      0x44000600ul
#define BK7258_REBOOT_TAG_ADDR   0x20003ff8ul

/* Vendor reset_reason.h REBOOT_TAG_REQ: a set tag makes the BootROM warm
 * fast-jump into the running app's startup trampoline (planned reboot);
 * a cleared tag plus a WATCHDOG reason runs the full cold chain so BL2
 * opens the download window. */

#define BK7258_REBOOT_TAG_REQ    0xaa55aa55ul

/* The board library build does not see the arch-internal inline register
 * accessors, so use plain volatile pointer access here. */

static inline uint32_t bk7258_reg_rd(uintptr_t addr)
{
  return *(volatile uint32_t *)addr;
}

static inline void bk7258_reg_wr(uintptr_t addr, uint32_t value)
{
  *(volatile uint32_t *)addr = value;
}

#ifdef CONFIG_BK7258_COMPONENT_CP
/****************************************************************************
 * Name: bk7258_aon_wdt_chip_reset
 *
 * Description:
 *   Force a chip-level reset through the AON WDT.  Mirrors
 *   wdt_hal_force_reboot(); the only difference between a planned reboot
 *   and a bootloader entry is the warm tag and the recorded reset reason.
 *
 *   SYSRESETREQ (up_systemreset) is not usable on this AMP part: it only
 *   resets CPU0 and leaves CPU1/RPTUN/shared-device state behind, which
 *   wedges the reboot chain ("Rptun driver reset remote ap" is the last
 *   line).  The AON WDT resets the device at the PMU level like the reset
 *   button, so both cores restart cleanly.  (dev 16e61a3, hand-merged
 *   with the AP bootloader-request path.)
 *
 *   The RPTUN reboot notifier has already reset the AP by the time the
 *   reboot chain reaches board_reset(); this chip reset covers CPU1 again
 *   regardless, and the normal bring-up re-releases it afterwards.
 *
 ****************************************************************************/

static void bk7258_aon_wdt_chip_reset(bool enter_bootloader)
{
  uint32_t val;

  if (enter_bootloader)
    {
      /* Clear the vendor warm fast-jump tag and record
       * RESET_SOURCE_WATCHDOG (0x2): BootROM runs the full cold chain and
       * BL2 opens the serial download handshake window.  The AON PMU
       * survives the chip reset, so the reason written here is what the
       * ROM/BL2 reads to classify the next boot; without it the ROM keeps
       * skipping BL2 even though the tag is gone.  AP reason lives in R0
       * bits[24:30] (7 bits), CP reason in bits[4:11] (8 bits). */

      bk7258_reg_wr(BK7258_REBOOT_TAG_ADDR, 0ul);

      val = bk7258_reg_rd(BK7258_AON_PMU_R0_ADDR);
      val &= ~((0x7ful << 24) | (0xfful << 4));
      val |= (0x2ul << 24) | (0x2ul << 4);
      bk7258_reg_wr(BK7258_AON_PMU_R0_ADDR, val);
    }
  else
    {
      /* Vendor planned reboot: set the warm tag so the BootROM fast-jumps
       * back into the startup trampoline.  The PMU-level reset is what
       * makes this path safe on AMP where SYSRESETREQ is not. */

      bk7258_reg_wr(BK7258_REBOOT_TAG_ADDR, BK7258_REBOOT_TAG_REQ);
    }

  /* Vendor aon_pmu_drv_wdt_change_not_rosc_clk(): select clk_DIVD as the
   * WDT LPO source so the emergency period bites deterministically. */

  val = bk7258_reg_rd(BK7258_AON_PMU_R41_ADDR);
  val &= ~0x3ul;
  bk7258_reg_wr(BK7258_AON_PMU_R41_ADDR, val);

  /* Vendor aon_pmu_hal_wdt_rst_dev_enable(): gate the AON WDT through to
   * the PMU reset line, keeping every other R2 bit unchanged. */

  val = bk7258_reg_rd(BK7258_AON_PMU_R2_ADDR);
  val &= ~0x3ful;
  val |= 0x26ul;
  bk7258_reg_wr(BK7258_AON_PMU_R2_ADDR, val);

  /* Vendor wdt_hal_force_reboot(): arm the AON WDT.  The control word
   * carries the key in bits[23:16] and the period in the low bits; two
   * writes with different keys are required for the value to latch. */

  bk7258_reg_wr(BK7258_AON_WDT_BASE, 0x5a000aul);
  bk7258_reg_wr(BK7258_AON_WDT_BASE, 0xa5000aul);

  /* The reset comes from the PMU, not from here. */

  for (; ; );
}
#endif /* CONFIG_BK7258_COMPONENT_CP */

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the board.  On the CP every status takes the AON WDT chip-level
 *   reset: a planned reboot warm fast-jumps back into the startup
 *   trampoline via the vendor tag, ENTER_BOOTLOADER runs the cold chain so
 *   the BL2 download window opens.  SYSRESETREQ is not used there -- it
 *   hangs this AMP part.  On the AP the RPTUN reboot notifier, which runs
 *   before this, hands every reason to the CP (bk7258_rptun_reset returns
 *   -ENOTSUP), and the CP resets the whole chip with that reason; SYSRESETREQ
 *   here, which restarts CPU1 only, is reached only if the CP does not
 *   answer within RPTUN_STATUS_TIMEOUT_MS.
 *
 ****************************************************************************/

int board_reset(int status)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  bk7258_aon_wdt_chip_reset(
    status == (int)BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER);
#else
  (void)status;
  up_systemreset();
#endif

  return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */
