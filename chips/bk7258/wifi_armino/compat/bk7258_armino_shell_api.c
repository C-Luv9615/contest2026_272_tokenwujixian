/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_shell_api.c
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
 * The shell API that surviving Armino code calls
 *
 * components/bk_cli is out of the build: the Armino CLI lost its input device
 * when shell_uart.c was replaced, so it only ever registered command tables
 * nobody could invoke.  Eleven other archives still call into it for log
 * plumbing, and bk_init.c calls bk_cli_init(); this file provides those entry
 * points with the semantics the callers actually need now that logging goes to
 * syslog.
 *
 *   shell_log_flush()        was "drain the async log ring to the UART"; the
 *                            ring is gone and syslog writes synchronously, so
 *                            this is a no-op.
 *   shell_assert_out()       must NOT be a no-op: it is the assert output path
 *                            (cm_backtrace prints through it).  The authority
 *                            writes straight to a UART with interrupts
 *                            disabled, bypassing the log ring; the equivalent
 *                            here is NuttX's up_putc(), which is the same
 *                            emergency console path.  bContinue == false keeps
 *                            the authority's "stop here" behaviour.
 *   shell_cmd_ind_out()      CLI prompt indicator -- nothing to indicate.
 *   shell_set_log_cpu()      which core owns the log ring -- no ring.
 *   shell_set_uart_port()    forwards to bk_set_printf_port(), as before.
 *   shell_set_log_level()    stores the level; nothing filters on it now that
 *                            printf does not consult the shell.
 *   bk_cli_init()            registers command tables; returns kNoErr.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <common/bk_err.h>
#include <common/bk_typedef.h>
#include <components/log.h>
#include <components/shell_task.h>
#include <driver/uart.h>
#include <nuttx/arch.h>

/* components/bk_cli/include/bk_api_cli.h:21 declares this; the component is no
 * longer built, so the signature is restated here rather than pulling its
 * private include directory into the compat layer.
 */

int bk_cli_init(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int s_shell_log_level = BK_LOG_LEVEL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk_cli_init(void)
{
  return 0;  /* kNoErr */
}

void shell_log_flush(void)
{
  /* Logging is synchronous now; nothing is queued to flush. */
}

int shell_assert_out(bool b_continue, char *format, ...)
{
  char    buf[CONFIG_PRINTF_BUF_SIZE];
  va_list args;
  int     i;

  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  /* Emergency path: write straight to the console, not through syslog. */

  for (i = 0; buf[i] != '\0' && i < (int)sizeof(buf); i++)
    {
      up_putc(buf[i]);
    }

  if (!b_continue)
    {
      for (; ; );
    }

  return 0;
}

void shell_cmd_ind_out(const char *format, ...)
{
  (void)format;
}

void shell_set_log_cpu(u8 req_cpu)
{
  (void)req_cpu;
}

void shell_set_uart_port(uint8_t uart_port)
{
  if (uart_port < UART_ID_MAX)
    {
      bk_set_printf_port(uart_port);
    }
}

void shell_set_log_level(int level)
{
  s_shell_log_level = level;
}

int shell_get_log_level(void)
{
  return s_shell_log_level;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
