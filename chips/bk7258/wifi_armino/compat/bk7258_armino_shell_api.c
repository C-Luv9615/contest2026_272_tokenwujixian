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
 * components/at_server is out of the build for the same reason, and the AT
 * front end needs the same treatment: its command and event device is the
 * shell_uart above, which has no input path, so no AT command ever arrives and
 * nothing it produced could ever leave.  wifi_at.c and bk_system_at.c stay
 * compiled -- they are its clients -- and the entry points they call are the
 * no-ops further down, next to the AT environment storage they read.
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

#include "at_server.h"

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

/****************************************************************************
 * The AT front end's entry points (components/at_server is excluded)
 *
 * Same situation as the CLI: at_server_init() and atsvr_app_init() are called
 * from bk_init.c:275/334, the command device is the input-less shell_uart, and
 * wifi_at.c / bk_system_at.c register tables and report through these calls.
 * With no transport there is nothing to register into and nowhere to send a
 * response, so registration reports success and the senders do nothing.
 *
 * _at_svr_env is real storage rather than a symbol stub because wifi_at.c
 * takes its address and reads fields from it; an all-zero environment is what
 * an at_server that never initialised would have left behind.
 ****************************************************************************/

_at_svr_ctrl_env_t _at_svr_env = {0};

void at_server_init(void)
{
}

int atsvr_app_init(void)
{
  return ATSVR_OK;
}

int atsvr_register_commands(const struct _atsvr_command *commands,
                            int num_commands, char *tag,
                            atsvr_event_cb evecb)
{
  (void)commands;
  (void)num_commands;
  (void)tag;
  (void)evecb;

  /* Nothing will ever match a command, so refusing the table would only make
   * the callers log a failure they cannot act on.
   */

  return ATSVR_OK;
}

void atsvr_cmd_rsp_ok(void)
{
}

void atsvr_cmd_rsp_error(void)
{
}

void atsvr_output_msg(char *msg)
{
  (void)msg;
}

void atsvr_event_sender(char *event, int sub_evt_id, int msg_len, void *msg,
                        bool free_msg)
{
  (void)event;
  (void)sub_evt_id;
  (void)msg_len;
  (void)msg;
  (void)free_msg;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
