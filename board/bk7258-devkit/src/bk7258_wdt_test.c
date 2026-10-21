/****************************************************************************
 * board/bk7258-devkit/src/bk7258_wdt_test.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * "bk7258_wdttest" -- exercise the ported Armino watchdog and reset-reason
 * face from the CP shell.  Test-only: enabled by CONFIG_BK7258_WDT_TEST,
 * which must be off in a delivered image.
 *
 * The register work lives in the chip layer
 * (chips/bk7258/bk7258_reset_reason.c) because the apps tree cannot see the
 * baseline headers; this file is the command front end only.
 *
 *   bk7258_wdttest peek          read AON PMU R0 / R7A and the cached reason
 *   bk7258_wdttest stop          bk_wdt_stop() + bk_task_wdt_stop()
 *   bk7258_wdttest start <ms>    bk_wdt_start(ms); 0 means the 8 s default
 *   bk7258_wdttest feed          bk_wdt_feed()
 *   bk7258_wdttest reason <n>    record reason n in both PMU domains
 *   bk7258_wdttest reset         bk_wdt_force_reboot(); never returns
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WDT_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk7258_reset.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_wdttest_help(void)
{
  printf("usage: bk7258_wdttest peek|stop|feed|sensor|reset\n"
         "       bk7258_wdttest start <ms>\n"
         "       bk7258_wdttest reason <n>\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc < 2)
    {
      bk7258_wdttest_help();
      return 0;
    }

  if (strcmp(argv[1], "peek") == 0)
    {
      bk7258_wdt_test_peek();
    }
  else if (strcmp(argv[1], "stop") == 0)
    {
      printf("bk7258_wdttest: bk_wdt_stop -> %d\n",
             bk7258_wdt_test_stop());
      printf("bk7258_wdttest: interrupt watchdog disarmed; the board must "
             "stay up now\n");
    }
  else if (strcmp(argv[1], "start") == 0)
    {
      unsigned long ms = argc > 2 ? strtoul(argv[2], NULL, 10) : 0;
      printf("bk7258_wdttest: bk_wdt_start(%lu) -> %d\n", ms,
             bk7258_wdt_test_start((unsigned int)ms));
      printf("bk7258_wdttest: feed it before the period elapses, or the "
             "chip resets\n");
    }
  else if (strcmp(argv[1], "feed") == 0)
    {
      printf("bk7258_wdttest: bk_wdt_feed -> %d\n", bk7258_wdt_test_feed());
    }
  else if (strcmp(argv[1], "reason") == 0)
    {
      if (argc < 3)
        {
          bk7258_wdttest_help();
          return 0;
        }

      printf("bk7258_wdttest: reason(0x%lx) -> %d\n",
             strtoul(argv[2], NULL, 0),
             bk7258_wdt_test_setreason(
               (unsigned int)strtoul(argv[2], NULL, 0)));
      bk7258_wdt_test_peek();
    }
  else if (strcmp(argv[1], "sensor") == 0)
    {
      printf("bk7258_wdttest: sensor -> %d\n", bk7258_wdt_test_sensor());
    }
  else if (strcmp(argv[1], "reset") == 0)
    {
      printf("bk7258_wdttest: bk_wdt_force_reboot()\n");
      fflush(stdout);
      bk7258_wdt_test_force_reset();
    }
  else
    {
      bk7258_wdttest_help();
    }

  return 0;
}

#endif /* CONFIG_BK7258_WDT_TEST */
