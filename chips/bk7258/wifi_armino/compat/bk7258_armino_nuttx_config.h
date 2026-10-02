/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_nuttx_config.h
 *
 * Force-included into the Armino driver component (docs/compose/spec/
 * bk7258-wifi-native.md, S2.7). NuttX's <nuttx/config.h> reaches Armino
 * units through the libc headers and its CONFIG_* names share the Armino
 * namespace. Only one leaks into a kept unit's #if: CONFIG_SPI (NuttX 1,
 * baseline sdkconfig undefined), at cp/middleware/driver/common/driver.c:356.
 * Pulling config.h in first and dropping that name keeps the baseline
 * branch; config.h's include guard stops it coming back.
 ****************************************************************************/

#include <nuttx/config.h>

#undef CONFIG_SPI
