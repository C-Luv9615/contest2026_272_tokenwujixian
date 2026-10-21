/****************************************************************************
 * chips/bk7258/bk7258_printf.c
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
 * Armino log transport, on the NuttX channel
 *
 * Replaces components/bk_system/printf.c (20 symbols) and printf_base.c
 * (6 symbols).  BK_LOGx expands to bk_printf_ext(level, tag, fmt, ...), and
 * both closed libraries call it directly (nm -u libwifi.a shows
 * bk_printf_ext and bk_printf_raw; libbk_phy.a adds bk_get/set_printf_sync),
 * so this is the single point every vendor log line passes through -- ours and
 * the closed libraries'.
 *
 * What stays identical: the wire format
 *
 *   [cpu0:][tag:]E|W|I|D|V(ms):
 *
 * the level filter, the per-module disable list and its whitelist flag, the
 * sync/enable switches, and CONFIG_PRINTF_BUF_SIZE truncation.  History is
 * full of cells captured against that format, so it is not touched.
 *
 * What changes: the sink.  Instead of shell_log_out_port() -> ring buffer ->
 * bk_uart_write_bytes() -> uart_driver -> uart_ll, the formatted buffer goes to
 * syslog().  That is the convergence: one log channel, NuttX's, and the UART
 * driver is no longer on the log path at all.
 *
 * Deliberate consequence: bk_printf_init() no longer calls bk_uart_init().
 * UART0 belongs to the NuttX console driver (chips/bk7258/bk7258_uart.c);
 * re-initialising it from the log layer would reprogram the port underneath the
 * console.  bk_get/set_printf_port() still track the port number because the
 * API does, but no hardware is touched here.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <common/bk_err.h>
#include <common/bk_typedef.h>
#include <common/sys_config.h>
#include <components/log.h>
#include <driver/uart.h>

#include "printf_impl.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_PRINTF_BUF_SIZE
#  define CONFIG_PRINTF_BUF_SIZE 128
#endif

#define BK7258_MAX_TAG_LEN       8
#define BK7258_MAX_INFO_LEN      14
#define BK7258_CPU_STR_LEN       4
#define BK7258_MOD_LIST_SLOTS    6
#define BK7258_MOD_NAME_LEN      11

#define BK7258_LOG_COMMON_MODE   0x03

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  char    mod_name[BK7258_MOD_NAME_LEN];
  uint8_t disabled;
} bk7258_mod_disable_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t s_printf_enable = 1;
static bool    s_printf_init;
static uint8_t s_print_port = CONFIG_UART_PRINT_PORT;

static volatile uint8_t s_printf_sync;
static bk7258_mod_disable_t s_mod_tag_list[BK7258_MOD_LIST_SLOTS];
static uint8_t s_whitelist_enabled;

/* Same table as the authority, indexed by BK_LOG_* level. */

static const char *const g_level_format[] =
{
  "(%d):",
  "E(%d):",
  "W(%d):",
  "I(%d):",
  "D(%d):",
  "V(%d):",
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Kept out of line on purpose: the baseline had printf.c calling this from
 * every entry point across a translation-unit boundary, so the symbol stayed
 * in the image.  Both units are ours now and the compiler would inline it,
 * which silently removes printf_is_init (and, through the same effect,
 * printf_lock_deinit / bk_printf_deinit) from the map.
 */

__attribute__((noinline)) bool printf_is_init(void)
{
  return s_printf_init;
}

int printf_lock_init(void)
{
  memset(&s_mod_tag_list[0], 0, sizeof(s_mod_tag_list));
  return BK_OK;
}

/* Same reason as printf_is_init: the baseline called this from another
 * translation unit, so the symbol stayed in the image; keep it out of line.
 */

/* retain, not used: the body is side-effect free, so GCC can drop the call
 * from bk_printf_deinit and the linker then collects the symbol.  retain marks
 * the section SHF_GNU_RETAIN so --gc-sections leaves it alone.
 */

__attribute__((noinline, retain)) int printf_lock_deinit(void)
{
  return BK_OK;
}

static int bk7258_mod_printf_disabled(const char *tag)
{
  int i;

  for (i = 0; i < BK7258_MOD_LIST_SLOTS; i++)
    {
      if (s_mod_tag_list[i].disabled)
        {
          if (strncmp(s_mod_tag_list[i].mod_name, tag,
                      sizeof(s_mod_tag_list[i].mod_name) - 1) == 0)
            {
              return 1;
            }
        }
    }

  return 0;
}

/* Build the authority's prefix, then hand the whole line to NuttX's syslog.
 * One buffer, one call: syslog("%s", buf) so that a '%' inside a vendor
 * message cannot be reinterpreted as a format directive.
 */

static void bk7258_log_out(const char *prefix, const char *fmt, va_list args)
{
  char string[CONFIG_PRINTF_BUF_SIZE];
  int  len;

  if (!printf_is_init() || !s_printf_enable)
    {
      return;
    }

  len = snprintf(string, sizeof(string), "%s", prefix);
  if (len < 0)
    {
      return;
    }

  if (len >= (int)sizeof(string))
    {
      len = sizeof(string) - 1;
    }

  vsnprintf(&string[len], sizeof(string) - len, fmt, args);
  string[sizeof(string) - 1] = 0;

  syslog(LOG_INFO, "%s", string);
}

static void bk7258_raw_out(const char *fmt, va_list args)
{
  char string[CONFIG_PRINTF_BUF_SIZE];

  if (!printf_is_init() || !s_printf_enable)
    {
      return;
    }

  vsnprintf(&string[0], sizeof(string), fmt, args);
  string[sizeof(string) - 1] = 0;

  syslog(LOG_INFO, "%s", string);
}

static void bk7258_prefix(char *prefix_str, int level, const char *tag)
{
  int data_len = 0;
  int cpu_id = up_cpu_index();

  prefix_str[0] = 0;

  if (cpu_id >= 0)
    {
      const char *cpu_prefix;
      int core_num;

      if (cpu_id == 0)
        {
          cpu_prefix = "cpu";
          core_num = 0;
        }
      else if (cpu_id == 1)
        {
          cpu_prefix = "ap";
          core_num = 0;
        }
      else if (cpu_id == 2)
        {
          cpu_prefix = "ap";
          core_num = 1;
        }
      else
        {
          cpu_prefix = "cpu";
          core_num = cpu_id;
        }

      snprintf(&prefix_str[data_len], BK7258_CPU_STR_LEN + 1, "%s%d",
               cpu_prefix, core_num);

      data_len += strlen(cpu_prefix) + 1;
      prefix_str[data_len++] = ':';
      prefix_str[data_len] = 0;
    }

  if (tag != NULL && tag[0] != 0)
    {
      strncpy(&prefix_str[data_len], tag, BK7258_MAX_TAG_LEN);
      prefix_str[BK7258_MAX_TAG_LEN + data_len] = 0;

      data_len = strlen(prefix_str);
      prefix_str[data_len] = ':';
      data_len++;
      prefix_str[data_len] = 0;
    }

  snprintf(&prefix_str[data_len], BK7258_MAX_INFO_LEN + 1,
           g_level_format[level], clock_systime_ticks() / 1000);
}

static void bk7258_printf_ext_internal(int level, char *tag, const char *fmt,
                                       va_list args)
{
  char prefix_str[(BK7258_CPU_STR_LEN + 1) + (BK7258_MAX_TAG_LEN + 1) +
                  BK7258_MAX_INFO_LEN + 1];

  if (!printf_is_init() || !s_printf_enable)
    {
      return;
    }

  if (tag != NULL && (bk7258_mod_printf_disabled(tag) ^ s_whitelist_enabled))
    {
      return;
    }

  bk7258_prefix(prefix_str, level, tag);
  bk7258_log_out(prefix_str, fmt, args);
}

static void bk7258_printf_raw_internal(int level, char *tag, const char *fmt,
                                       va_list args)
{
  if (!printf_is_init() || !s_printf_enable)
    {
      return;
    }

  if (tag != NULL && (bk7258_mod_printf_disabled(tag) ^ s_whitelist_enabled))
    {
      return;
    }

  bk7258_raw_out(fmt, args);
}

/****************************************************************************
 * Public Functions: printf.c face
 ****************************************************************************/

void bk_printf_ext(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_vprintf_ext(int level, char *tag, const char *fmt, va_list args)
{
  bk7258_printf_ext_internal(level, tag, fmt, args);
}

void bk_printf(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(BK_LOG_DEBUG, NULL, fmt, args);
  va_end(args);
}

void bk_printf_ex(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_printf_nonblock(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_printf_static_block(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_printf_static_nonblock(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_ext_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_printf_raw(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_raw_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_printf_raw_nonblock(int level, char *tag, const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  bk7258_printf_raw_internal(level, tag, fmt, args);
  va_end(args);
}

void bk_vprintf_raw(int level, char *tag, const char *fmt, va_list args)
{
  bk7258_printf_raw_internal(level, tag, fmt, args);
}

void bk_buf_printf_sync(char *buf, int buf_len)
{
  /* Obsoleted API in the authority; kept as the same no-op. */

  (void)buf;
  (void)buf_len;
}

void bk_set_printf_enable(uint8_t enable)
{
  s_printf_enable = enable;
}

void bk_set_printf_sync(uint8_t enable)
{
#if CONFIG_SYS_PRINT_DEV_MAILBOX
  (void)enable;
  s_printf_sync = 0;
#else
  s_printf_sync = enable;
#endif
}

int bk_get_printf_sync(void)
{
  return s_printf_sync;
}

void bk_enable_white_list(int enabled)
{
  s_whitelist_enabled = enabled ? 1 : 0;
}

int bk_white_list_state(void)
{
  return s_whitelist_enabled;
}

void bk_disable_mod_printf(char *mod_name, uint8_t disable)
{
  int i;
  int slot = -1;

  disable = disable ^ s_whitelist_enabled;

  if (disable == 0)
    {
      for (i = 0; i < BK7258_MOD_LIST_SLOTS; i++)
        {
          if (s_mod_tag_list[i].disabled &&
              strncmp(s_mod_tag_list[i].mod_name, mod_name,
                      sizeof(s_mod_tag_list[i].mod_name) - 1) == 0)
            {
              s_mod_tag_list[i].disabled = 0;
            }
        }

      return;
    }

  for (i = 0; i < BK7258_MOD_LIST_SLOTS; i++)
    {
      if (!s_mod_tag_list[i].disabled)
        {
          slot = i;
          break;
        }
    }

  if (slot < 0)
    {
      return;
    }

  strncpy(s_mod_tag_list[slot].mod_name, mod_name,
          sizeof(s_mod_tag_list[slot].mod_name) - 1);
  s_mod_tag_list[slot].mod_name[sizeof(s_mod_tag_list[slot].mod_name) - 1] = 0;
  s_mod_tag_list[slot].disabled = 1;
}

char *bk_get_disable_mod(int *idx)
{
  if (idx == NULL)
    {
      return NULL;
    }

  if (*idx < 0 || *idx >= BK7258_MOD_LIST_SLOTS)
    {
      return NULL;
    }

  return s_mod_tag_list[*idx].mod_name;
}

/****************************************************************************
 * Public Functions: printf_base.c face
 ****************************************************************************/

bk_err_t bk_printf_init(void)
{
  int ret;

  ret = printf_lock_init();
  if (BK_OK != ret)
    {
      return ret;
    }

  /* No bk_uart_init() here: UART0 is the NuttX console's port.  The log layer
   * only formats and hands the line to syslog.
   */

  s_printf_init = true;

  return BK_OK;
}

__attribute__((noinline)) bk_err_t bk_printf_deinit(void)
{
  s_printf_init = false;
  printf_lock_deinit();
  return BK_OK;
}

void bk_null_printf(const char *fmt, ...)
{
  (void)fmt;
}

int bk_get_printf_port(void)
{
  return s_print_port;
}

void bk_set_printf_port(uint8_t port_num)
{
  if (s_print_port != port_num && port_num < UART_ID_MAX)
    {
      bk_printf_deinit();
      s_print_port = port_num;
      bk_printf_init();
    }
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
