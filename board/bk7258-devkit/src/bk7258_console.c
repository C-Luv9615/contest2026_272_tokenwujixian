/****************************************************************************
 * board/bk7258-devkit/src/bk7258_console.c
 *
 * CP init entry (CONFIG_INIT_ENTRYPOINT).  UART0 stays on the CP, which
 * owns the AP lifecycle and the early boot log.  With
 * BK7258_CONSOLE_AUTO_AP the console is handed to the AP NSH through cu
 * once the AP scheduler is running; "~." in cu falls back to the CP NSH,
 * and "cu" from there goes back.  The init task is the only console
 * reader at any time: cu first, then nsh_main.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/boardctl.h>
#include <sys/wait.h>

#include <arch/chip/bk7258_ap_boot.h>

int nsh_main(int argc, FAR char *argv[]);

#ifdef CONFIG_BK7258_CONSOLE_AUTO_AP
int cu_main(int argc, FAR char *argv[]);

#define BK7258_CONSOLE_POLL_MS 200

/* The AP health thread starts in the AP board_late_initialize(), just
 * before the AP NSH, and bumps the heartbeat once a second. */

static bool bk7258_console_ap_ready(void)
{
  struct bk7258_ap_boot_record_s record;

  return bk7258_ap_record_read(&record) &&
         record.stage == BK7258_AP_STAGE_SCHEDULER_RUNNING &&
         record.heartbeat >= 2;
}

static void bk7258_console_attach_ap(void)
{
  FAR char *argv[] =
  {
    "cu", "-l", "/dev/ttyAP", NULL
  };

  int polls = 0;
  pid_t pid;

  while (!bk7258_console_ap_ready())
    {
      if (++polls * BK7258_CONSOLE_POLL_MS >
          CONFIG_BK7258_CONSOLE_AUTO_AP_TIMEOUT * 1000)
        {
          printf("\n[console] AP not running after %d s, "
                 "staying on the CP NSH\n",
                 CONFIG_BK7258_CONSOLE_AUTO_AP_TIMEOUT);
          return;
        }

      usleep(BK7258_CONSOLE_POLL_MS * 1000);
    }

  printf("\n[console] attached to the AP NSH; "
         "\"~.\" returns to the CP NSH, \"cu\" comes back\n");

  /* A task of its own: cu installs a SIGINT handler it never restores,
   * which must not outlive it into the CP NSH. */

  pid = task_create("cu", CONFIG_SYSTEM_CUTERM_PRIORITY,
                    CONFIG_SYSTEM_CUTERM_STACKSIZE, cu_main, &argv[1]);
  if (pid < 0)
    {
      printf("[console] cu start failed: %d\n", pid);
      return;
    }

  waitpid(pid, NULL, 0);
}
#endif

int bk7258_cp_init_main(int argc, FAR char *argv[])
{
#ifdef CONFIG_BK7258_CONSOLE_AUTO_AP
  /* What nsh_main would do first (procfs, LCD), so the CP is fully set up
   * while the console sits on the AP. */

  boardctl(BOARDIOC_INIT, 0);
  bk7258_console_attach_ap();
#endif

  return nsh_main(argc, argv);
}
