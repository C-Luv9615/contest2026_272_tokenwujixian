/****************************************************************************
 * chips/bk7258/bk7258_urandom.c
 *
 * /dev/urandom over the BK7258 hardware TRNG, for the ai_agent TLS stack
 * (packages/ai_agent/src/infra/vela_tls.c: agent_secure_random() opens
 * /dev/urandom and refuses to fall back to anything predictable, so the
 * device has to exist and be backed by real entropy).
 *
 * The TRNG block at 0x458c0000 is a read-only entropy source with no clock
 * gate to manage beyond bypassing it, which the enable sequence below does.
 * It is shared read-only across CP and AP: both cores may read it, and the
 * CP's Armino bk_trng_driver_init() already enabled it during driver init.
 * This driver enables it and never disables it, so a later bk_trng_stop()
 * on the CP is the only way the source can go quiet.
 *
 * Registers (trng_struct.h): +0x08 global_ctrl (bit0 soft_reset, bit1
 * clk_gate_bypass), +0x10 ctrl (bit0 en), +0x14 data.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>

#include "arm_internal.h"
#include "bk7258_memorymap.h"

#define TRNG_GLOBAL_CTRL  (BK7258_TRNG_BASE + UINT32_C(0x08))
#define TRNG_CTRL         (BK7258_TRNG_BASE + UINT32_C(0x10))
#define TRNG_DATA         (BK7258_TRNG_BASE + UINT32_C(0x14))

#define TRNG_SOFT_RESET        (UINT32_C(1) << 0)
#define TRNG_CLK_GATE_BYPASS   (UINT32_C(1) << 1)
#define TRNG_ENABLE            (UINT32_C(1) << 0)

static bool g_trng_enabled;

/* trng_ll_enable(): bypass the clock gate or the first read of data comes
 * back the same every time (trng_ll.h:59).
 */

static void bk7258_trng_enable(void)
{
  if (g_trng_enabled)
    {
      return;
    }

  putreg32(TRNG_SOFT_RESET, TRNG_GLOBAL_CTRL);
  putreg32(TRNG_CLK_GATE_BYPASS, TRNG_GLOBAL_CTRL);
  putreg32(TRNG_ENABLE, TRNG_CTRL);
  g_trng_enabled = true;
}

static ssize_t bk7258_urandom_read(FAR struct file *filep, FAR char *buffer,
                                   size_t buflen)
{
  FAR uint8_t *out = (FAR uint8_t *)buffer;
  size_t left = buflen;

  (void)filep;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  bk7258_trng_enable();

  while (left > 0)
    {
      uint32_t word = getreg32(TRNG_DATA);
      size_t take = left < sizeof(word) ? left : sizeof(word);

      memcpy(out, &word, take);
      out += take;
      left -= take;
    }

  return (ssize_t)buflen;
}

static const struct file_operations g_urandom_ops =
{
  .read = bk7258_urandom_read,
};

void bk7258_urandom_register(void)
{
  int ret;

  bk7258_trng_enable();
  ret = register_driver("/dev/urandom", &g_urandom_ops, 0444, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] urandom register failed: %d\n", ret);
    }
}
