/*
 * chips/bk7258/wifi/glue/include/driver/aon_rtc.h
 *
 * NuttX reimplementation of the Armino AON-RTC driver surface used by the
 * vendored glue. Skeleton: init/deinit are no-op success; period-timer create/
 * destroy return BK_ERR_NOT_SUPPORT until the team aon-rtc driver exists.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H
#define __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t aon_rtc_id_t;
typedef uint32_t rtc_tick_t;

bk_err_t bk_aon_rtc_driver_init(void);
bk_err_t bk_aon_rtc_driver_deinit(void);
uint64_t bk_aon_rtc_get_current_tick(aon_rtc_id_t id);
float bk_rtc_get_ms_tick_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_AON_RTC_H */
