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

#include <syslog.h>

#include <nuttx/kthread.h>

#define BK7258_ARMINO_ENTRY_PRIORITY  85
#define BK7258_ARMINO_ENTRY_STACKSIZE 8192

void entry_main(void);
int bk7258_armino_start(void);

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
