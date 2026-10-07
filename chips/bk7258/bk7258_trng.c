/****************************************************************************
 * chips/bk7258/bk7258_trng.c
 *
 * The BK7258 hardware TRNG (0x458c0000), owned in one place:
 *
 *   1. six symbols for the remaining Armino callers, and
 *   2. devurandom_register(), NuttX's /dev/urandom hook.
 *
 * (1) replaces three Armino objects — cp/middleware/driver/trng/trng_driver.c,
 * cp/middleware/soc/common/hal/trng_hal.c, cp/middleware/soc/bk7258/hal/
 * trng_ll.c (docs/compose/spec/bk7258-wifi-native.md S2.5 / T15).  The
 * callers stay unchanged: psa_mbedtls's platform.c (TLS entropy),
 * bk_system/mac.c, bk_init/components_init.c (srand + stack guard), and the
 * bk_cli tools.  Prototypes live in cp/include/driver/trng.h:47-96;
 * bk_err_t is int (cp/include/common/bk_err.h:45) and
 * BK_ERR_TRNG_DRIVER_NOT_INIT is BK_ERR_TRNG_BASE - 1 = -0x2701
 * (cp/include/driver/trng_types.h:31).  Those headers are not on the arch
 * include path, so the contract is restated here rather than pulled in.
 *
 * (2) is the arch half of CONFIG_DEV_URANDOM_ARCH (nuttx/drivers/crypto/
 * dev_urandom.c:50, Kconfig: "architecture-specific logic must provide the
 * whole implementation in this case, including the function
 * devurandom_register()").  With that option set, nuttx's software PRNG
 * compiles out and drivers_initialize() calls ours instead.  Both
 * /dev/random and /dev/urandom come off this one source (nuttx/drivers/
 * crypto/Kconfig: "/dev/urandom may refer to the same driver as
 * /dev/random"); CONFIG_DEV_RANDOM is default-y under ARCH_HAVE_RNG, so
 * devrandom_register() is required too.  The consumer that matters is
 * packages/ai_agent's agent_secure_random() and psa_mbedtls/library/
 * entropy_poll.c:181, both of which open /dev/urandom and refuse a
 * predictable fallback.
 *
 * Semantics of (1) follow the baseline source, not the NuttX idiom:
 *
 *   - start/stop power-gate the block per call, and bk_rand()/bk_fill_rand()
 *     enable it around their reads (trng_driver.c:74,87,104,131).
 *   - the first 16 reads after enable are discarded: boards sampling at the
 *     same time otherwise return the same word (trng_driver.c:82,105).
 *   - a 50 us wait after enable lets the clock-gate bypass take effect
 *     (trng_driver.c:75,104).
 *   - bk_rand() returns number & RAND_MAX (trng_driver.c:89).  RAND_MAX is
 *     INT_MAX in both NuttX and the baseline toolchain, so the mask is
 *     unchanged.
 *   - bk_fill_rand() writes whole little-endian words and takes the tail
 *     bytes from one more read, low byte first (trng_driver.c:118-127).
 *   - start/stop return BK_ERR_TRNG_DRIVER_NOT_INIT before init
 *     (trng_driver.c:24).
 *
 * The baseline's CONFIG_SOC_BK7256XX clock-gate dance inside
 * trng_get_random_number() is BK7256-only and is not compiled for this
 * chip, so it is not reproduced.
 *
 * Register sequence is trng_ll.h's: init = soft_reset then en=0
 * (trng_ll.h:50-54); enable = soft_reset + clk_gate_bypass then en=1
 * (trng_ll.h:55-60); disable = en=0 then clk_gate_bypass=0 (trng_ll.h:61-65).
 * Those are bitfield stores, i.e. read-modify-write of the whole register,
 * so enable ends with BOTH global_ctrl bits set.
 *
 * One deliberate substitution: the baseline waits with bk_delay_us() from
 * bk_system/delay.c, an Armino unit.  This file uses NuttX's up_udelay(),
 * a busy-wait of the same width, so the replacement does not depend on a
 * unit that phase two is still removing.
 *
 * Power-gating note.  bk_trng_stop() really does turn the block off, as the
 * baseline does.  /dev/urandom reads therefore re-enable it first: the block
 * is shared read-only across CP and AP, and either core's bk_trng_stop()
 * would otherwise leave the other core reading a quiet source.  The unit is
 * initialised from bk_trng_driver_init(), called by board_late_initialize()
 * and by devurandom_register() itself (whichever runs first), never from
 * Armino's driver_init() registry.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>

#include "arm_internal.h"
#include "bk7258_memorymap.h"

#define TRNG_GLOBAL_CTRL  (BK7258_TRNG_BASE + UINT32_C(0x08))
#define TRNG_CTRL         (BK7258_TRNG_BASE + UINT32_C(0x10))
#define TRNG_DATA         (BK7258_TRNG_BASE + UINT32_C(0x14))

#define TRNG_SOFT_RESET        (UINT32_C(1) << 0)
#define TRNG_CLK_GATE_BYPASS   (UINT32_C(1) << 1)
#define TRNG_ENABLE            (UINT32_C(1) << 0)

#define TRNG_READ_COUNT  16

#define BK_ERR_TRNG_BASE               (-0x2700)
#define BK_ERR_TRNG_DRIVER_NOT_INIT    (BK_ERR_TRNG_BASE - 1)

#define TRNG_DELAY_AFTER_ENABLE_US  50

static bool s_trng_driver_is_init;

static void trng_hw_init(void)
{
  putreg32(getreg32(TRNG_GLOBAL_CTRL) | TRNG_SOFT_RESET, TRNG_GLOBAL_CTRL);
  putreg32(getreg32(TRNG_CTRL) & ~TRNG_ENABLE, TRNG_CTRL);
}

static void trng_hw_enable(void)
{
  putreg32(getreg32(TRNG_GLOBAL_CTRL) | TRNG_SOFT_RESET | TRNG_CLK_GATE_BYPASS,
           TRNG_GLOBAL_CTRL);
  putreg32(getreg32(TRNG_CTRL) | TRNG_ENABLE, TRNG_CTRL);
}

static void trng_hw_disable(void)
{
  putreg32(getreg32(TRNG_CTRL) & ~TRNG_ENABLE, TRNG_CTRL);
  putreg32(getreg32(TRNG_GLOBAL_CTRL) & ~TRNG_CLK_GATE_BYPASS, TRNG_GLOBAL_CTRL);
}

static uint32_t trng_get_random_number(void)
{
  return getreg32(TRNG_DATA);
}

/* Fill a buffer from the block.  Used by bk_fill_rand() and /dev/urandom.
 * Does not power-gate: the caller owns the enable/disable decision.
 * Discards the first TRNG_READ_COUNT words of every enable window, as the
 * baseline does, so a caller that enables once and reads twice still gets
 * independent words.
 */

static void trng_fill(uint8_t *out, size_t len)
{
  size_t i;
  uint32_t rnd;
  size_t word_count = len / sizeof(uint32_t);
  size_t remain = len % sizeof(uint32_t);

  for (i = 0; i < word_count; i++)
    {
      rnd = trng_get_random_number();
      memcpy(out, &rnd, sizeof(uint32_t));
      out += sizeof(uint32_t);
    }

  if (remain > 0)
    {
      rnd = trng_get_random_number();
      for (i = 0; i < remain; i++)
        {
          out[i] = (uint8_t)((rnd >> (8 * i)) & 0xff);
        }
    }
}

int bk_trng_driver_init(void)
{
  if (s_trng_driver_is_init)
    {
      return 0;
    }

  trng_hw_init();
  s_trng_driver_is_init = true;

  return 0;
}

int bk_trng_driver_deinit(void)
{
  if (!s_trng_driver_is_init)
    {
      return 0;
    }

  trng_hw_disable();
  s_trng_driver_is_init = false;

  return 0;
}

int bk_trng_start(void)
{
  if (!s_trng_driver_is_init)
    {
      return BK_ERR_TRNG_DRIVER_NOT_INIT;
    }

  trng_hw_enable();
  return 0;
}

int bk_trng_stop(void)
{
  if (!s_trng_driver_is_init)
    {
      return BK_ERR_TRNG_DRIVER_NOT_INIT;
    }

  trng_hw_disable();
  return 0;
}

int bk_rand(void)
{
  int i;
  uint32_t number = 0;

  bk_trng_start();
  up_udelay(TRNG_DELAY_AFTER_ENABLE_US);

  for (i = 0; i < TRNG_READ_COUNT; i++)
    {
      trng_get_random_number();
    }

  number = trng_get_random_number();

  bk_trng_stop();

  return (int)(number & RAND_MAX);
}

int bk_fill_rand(void *buff, size_t len)
{
  size_t i;

  if (buff == NULL || len == 0)
    {
      return -1;
    }

  bk_trng_start();
  up_udelay(TRNG_DELAY_AFTER_ENABLE_US);

  for (i = 0; i < TRNG_READ_COUNT; i++)
    {
      trng_get_random_number();
    }

  trng_fill((uint8_t *)buff, len);

  bk_trng_stop();

  return 0;
}

/****************************************************************************
 * /dev/urandom — the NuttX face of the same block
 ****************************************************************************/

static ssize_t bk7258_urandom_read(FAR struct file *filep, FAR char *buffer,
                                   size_t buflen)
{
  (void)filep;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  /* Either core may have power-gated the block since the last read
   * (bk_trng_stop() on CP, or our own deinit), so re-enable first.
   */

  bk_trng_driver_init();
  trng_hw_enable();
  up_udelay(TRNG_DELAY_AFTER_ENABLE_US);

  for (size_t i = 0; i < TRNG_READ_COUNT; i++)
    {
      trng_get_random_number();
    }

  trng_fill((uint8_t *)buffer, buflen);

  return (ssize_t)buflen;
}

static const struct file_operations g_urandom_ops =
{
  .read = bk7258_urandom_read,
};

void devrandom_register(void)
{
  int ret;

  /* drivers_initialize() calls this one before devurandom_register().
   * /dev/random is the same hardware-backed source (nuttx/drivers/crypto/
   * Kconfig: "/dev/urandom may refer to the same driver as /dev/random").
   */

  bk_trng_driver_init();
  ret = register_driver("/dev/random", &g_urandom_ops, 0444, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] random register failed: %d\n", ret);
    }
}

void devurandom_register(void)
{
  int ret;

  bk_trng_driver_init();
  ret = register_driver("/dev/urandom", &g_urandom_ops, 0444, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] urandom register failed: %d\n", ret);
    }
}
