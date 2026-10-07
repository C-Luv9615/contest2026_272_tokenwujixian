/****************************************************************************
 * chips/bk7258/bk7258_trng.c
 *
 * Phase-two native replacement for the Armino TRNG unit
 * (docs/compose/spec/bk7258-wifi-native.md S2.5 / T15).  Replaces three
 * Armino objects:
 *
 *   cp/middleware/driver/trng/trng_driver.c
 *   cp/middleware/soc/common/hal/trng_hal.c
 *   cp/middleware/soc/bk7258/hal/trng_ll.c
 *
 * The six exported symbols keep the baseline contracts, so the remaining
 * Armino callers stay unchanged: psa_mbedtls's platform.c (TLS entropy),
 * bk_system/mac.c, bk_init/components_init.c (srand + stack guard), and
 * the bk_cli tools.  Prototypes live in cp/include/driver/trng.h:47-96;
 * bk_err_t is int (cp/include/common/bk_err.h:45) and
 * BK_ERR_TRNG_DRIVER_NOT_INIT is BK_ERR_TRNG_BASE - 1 = -0x2701
 * (cp/include/driver/trng_types.h:31).  Those headers are not on the arch
 * include path, so the contract is restated here rather than pulled in.
 *
 * Semantics follow the baseline source, not the NuttX idiom:
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
 * One deliberate substitution: the baseline waits with bk_delay_us() from
 * bk_system/delay.c, an Armino unit.  This file uses NuttX's up_udelay(),
 * a busy-wait of the same width, so the replacement does not depend on a
 * unit that phase two is still removing.
 *
 * The baseline's CONFIG_SOC_BK7256XX clock-gate dance inside
 * trng_get_random_number() is BK7256-only and is not compiled for this
 * chip, so it is not reproduced.
 *
 * Register sequence is trng_ll.h's: init = soft_reset then en=0
 * (trng_ll.h:50-54); enable = soft_reset + clk_gate_bypass then en=1
 * (trng_ll.h:55-60); disable = en=0 then clk_gate_bypass=0 (trng_ll.h:61-65).
 * Those are bitfield stores, i.e. read-modify-write of the whole register,
 * so enable ends with BOTH global_ctrl bits set.  bk7258_urandom.c writes
 * the two bits as two separate whole-register stores and so ends with only
 * clk_gate_bypass set; this file follows the baseline.  The block is shared
 * read-only with bk7258_urandom.c (/dev/urandom), and a bk_trng_stop() is
 * still the only way the source goes quiet for that device — same as before
 * this replacement.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
  uint32_t rnd;
  size_t word_count;
  size_t remain;
  uint8_t *p = (uint8_t *)buff;

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

  word_count = len / sizeof(uint32_t);
  remain = len % sizeof(uint32_t);

  for (i = 0; i < word_count; i++)
    {
      rnd = trng_get_random_number();
      memcpy(p, &rnd, sizeof(uint32_t));
      p += sizeof(uint32_t);
    }

  if (remain > 0)
    {
      rnd = trng_get_random_number();
      for (i = 0; i < remain; i++)
        {
          p[i] = (uint8_t)((rnd >> (8 * i)) & 0xff);
        }
    }

  bk_trng_stop();

  return 0;
}
