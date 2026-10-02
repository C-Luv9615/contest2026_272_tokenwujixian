/****************************************************************************
 * chips/bk7258/wifi_armino/bk7258_armino_start.c
 *
 * Phase-one entry for the Armino iperf baseline (docs/compose/spec/
 * bk7258-wifi-native.md, S2.2).
 *
 * On Armino, Reset_Handler reaches entry_main()
 * (cp/components/bk_startup/system_main.c:412) on the main stack before any
 * scheduler exists. NuttX owns reset and the scheduler, so entry_main() runs
 * here in a kernel thread instead. Everything it calls is unchanged Armino
 * source; the FreeRTOS API underneath, including rtos_start_scheduler(), is
 * the T9 seam.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <syslog.h>

#include <nuttx/kthread.h>

#include "arm_internal.h"

#define BK7258_ARMINO_ENTRY_PRIORITY  85
#define BK7258_ARMINO_ENTRY_STACKSIZE 8192

/* REBOOT_TAG_ADDR (cp/middleware/driver/reset_reason/reset_reason.h:28) is
 * 0x20003FF8 + SOC_ADDR_OFFSET, and SOC_ADDR_OFFSET is 0 because the
 * baseline sets CONFIG_SPE (cp/include/soc/bk7258/reg_base.h:22). The board
 * reset code uses the same slot (board/bk7258-devkit/src/bk7258_reset.c:64).
 */

#define BK7258_ARMINO_REBOOT_TAG_ADDR 0x20003ff8u

void entry_main(void);
int bk7258_armino_start(void);
void set_reboot_tag(uint32_t tag);

/* Two symbols of units NuttX replaces at reset:
 *
 * build_version comes from the baseline's generated link helper
 * build/.../project_elf_src.c, which is not an Armino component source.
 * Same definition; read by trap_base.c and at_server's _atsvr_func.c.
 */

volatile const char build_version[] = __DATE__ " " __TIME__;

/* set_reboot_tag() lives in the Armino reset entry
 * (cmsis/.../startup_bk7236.c:352), excluded because reset belongs to NuttX.
 * Its only caller outside that file is wdt_hal.c:94 (REBOOT_TAG_REQ before a
 * watchdog reboot); same body.
 */

void set_reboot_tag(uint32_t tag)
{
  putreg32(tag, BK7258_ARMINO_REBOOT_TAG_ADDR);
}

static int bk7258_armino_entry(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  entry_main();
  return 0;
}

int bk7258_armino_start(void)
{
  int ret;

  ret = kthread_create("armino-entry", BK7258_ARMINO_ENTRY_PRIORITY,
                       BK7258_ARMINO_ENTRY_STACKSIZE,
                       bk7258_armino_entry, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[ARMINO] entry thread failed: %d\n", ret);
      return ret;
    }

  return 0;
}
