/*
 * chips/bk7258/wifi/glue/system_shim.c
 *
 * NuttX implementation of the Armino system API (printf/reboot/mac/tick)
 * used by the vendored glue. printf maps onto NuttX stdio; reboot onto
 * up_systemreset. MAC reading is deferred to the chip layer (efuse/OTP).
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "components/system.h"

/****************************************************************************
 * MAC
 ****************************************************************************/

bk_err_t bk_set_base_mac(const uint8_t *mac)
{
  /* Chip-layer efuse/OTP base-MAC write is not wired yet. */
  (void)mac;
  return BK_FAIL;
}

bk_err_t bk_get_mac(uint8_t *mac, mac_type_t type)
{
  /* Chip-layer efuse/OTP MAC read is not wired yet (plan §11.3). */
  (void)mac;
  (void)type;
  return BK_FAIL;
}

/****************************************************************************
 * Reboot
 ****************************************************************************/

void bk_reboot(void)
{
  up_systemreset();
}

void bk_reboot_ex(uint32_t reset_reason)
{
  (void)reset_reason;
  up_systemreset();
}

/****************************************************************************
 * Tick / time
 ****************************************************************************/

uint64_t bk_get_tick(void)
{
  return (uint64_t)clock_systime_ticks();
}

uint32_t bk_get_second(void)
{
  return (uint32_t)(clock_systime_ticks() / TICK_PER_SECOND);
}

uint32_t bk_get_ms_per_tick(void)
{
  return (uint32_t)MSEC_PER_TICK;
}

uint32_t bk_get_ticks_per_second(void)
{
  return (uint32_t)TICK_PER_SECOND;
}

/****************************************************************************
 * printf family
 ****************************************************************************/

int bk_printf_init(void)
{
  return 0;
}

int bk_printf_deinit(void)
{
  return 0;
}

void bk_printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void bk_null_printf(const char *fmt, ...)
{
  (void)fmt;
}

void bk_printf_ex(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  printf("[%s] ", tag);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_printf_ext(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  printf("[%s] ", tag);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_printf_raw(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  (void)tag;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  (void)level;
}

void bk_vprintf_ext(int level, char *tag, const char *fmt, va_list args)
{
  printf("[%s] ", tag);
  vprintf(fmt, args);
  (void)level;
}

void bk_vprintf_raw(int level, char *tag, const char *fmt, va_list args)
{
  (void)tag;
  vprintf(fmt, args);
  (void)level;
}

void bk_set_printf_enable(uint8_t enable)
{
  (void)enable;
}

void bk_set_printf_sync(uint8_t enable)
{
  (void)enable;
}

void bk_set_printf_port(uint8_t port_num)
{
  (void)port_num;
}

int bk_get_printf_port(void)
{
  return 0;
}

/****************************************************************************
 * Reset reason
 ****************************************************************************/

static uint32_t g_bk_reset_reason = RESET_SOURCE_UNKNOWN;

uint32_t bk_misc_get_reset_reason(void)
{
  return g_bk_reset_reason;
}

void bk_misc_set_reset_reason(uint32_t type)
{
  g_bk_reset_reason = type;
}

uint32_t bk_misc_get_cp_reset_reason(void)
{
  return g_bk_reset_reason;
}

uint32_t bk_misc_get_ap_reset_reason(void)
{
  return g_bk_reset_reason;
}

void bk_misc_set_ap_reset_reason(uint32_t type)
{
  (void)type;
}
