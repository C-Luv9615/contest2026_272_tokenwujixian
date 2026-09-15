/*
 * chips/bk7258/wifi/glue/include/gpio_driver.h
 *
 * NuttX reimplementation of the Armino GPIO-device mapping used only by the
 * vendor debug-GPIO hook. Skeleton: mapping is a no-op returning OK until the
 * team gpio layer exposes device muxing.
 */

#ifndef __BK7258_WIFI_GLUE_GPIO_DRIVER_H
#define __BK7258_WIFI_GLUE_GPIO_DRIVER_H

#include <stdint.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t gpio_id_t;
typedef uint32_t gpio_dev_t;

#define GPIO_2            2
#define GPIO_3            3
#define GPIO_4            4
#define GPIO_DEV_DEBUG0   0xC0u
#define GPIO_DEV_DEBUG1   0xC1u
#define GPIO_DEV_DEBUG2   0xC2u
#define GPIO_DEV_RXEN     0xD0u
#define GPIO_DEV_TXEN     0xD1u

bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);
bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_GPIO_DRIVER_H */
