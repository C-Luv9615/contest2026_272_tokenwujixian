/****************************************************************************
 * board/bk7258-devkit/src/bk7258_panic_test.c
 *
 * "bk7258_panic [fault]": force a kernel panic on the core it runs on, to
 * exercise the dual-core dump (BK7258_RPTUN_PANIC_PEER).  A plain PANIC()
 * from an NSH task is only a task assertion in a flat build, so call
 * _assert() as an interrupt-level fault would; "fault" takes a real
 * UsageFault through the exception path instead.
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "fault") == 0)
    {
      printf("bk7258_panic: undefined instruction\n");
      fflush(stdout);
      __builtin_trap();
    }

  printf("bk7258_panic: kernel panic\n");
  fflush(stdout);
  _assert(__FILE__, __LINE__, "bk7258_panic test", NULL, true);
  return 0;
}
