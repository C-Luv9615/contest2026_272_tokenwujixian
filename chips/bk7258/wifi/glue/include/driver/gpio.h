/*
 * chips/bk7258/wifi/glue/include/driver/gpio.h
 *
 * GPIO surface needed by the vendored bk_phy_adapter.c, which muxes the RF
 * TX/RX-enable pins (gpio_dev_map_rxen/txen at lines 180-188) and wraps
 * bk_gpio_pull_down for the PHY capability table.
 *
 * Builds on gpio_driver.h, which already carries gpio_id_t / gpio_dev_t and the
 * gpio_dev_map/unmap declarations; this header only adds what bk_phy needs.
 *
 * On the RXEN/TXEN values: upstream pins GPIO_DEV_RXEN to 0x70
 * (hal_gpio_types.h:304) but leaves GPIO_DEV_TXEN positional inside a long
 * enum, so its number falls out of the whole list rather than being written
 * down. Instead of guessing, that enum was walked from its last explicit anchor
 * (GPIO_DEV_QSPI_RAM_CLK = 0x60) to GPIO_DEV_RXEN, which confirms
 * GPIO_DEV_TXEN = 0x6f -- exactly one below RXEN. Both values are upstream's.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_GPIO_H
#define __BK7258_WIFI_GLUE_DRIVER_GPIO_H

#include <common/bk_err.h>

#include "gpio_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO_DEV_RXEN / GPIO_DEV_TXEN are defined in gpio_driver.h, included above --
 * one source of truth for the mux tokens.
 */

bk_err_t bk_gpio_pull_down(gpio_id_t gpio_id);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_GPIO_H */
