/*
 * chips/bk7258/wifi/glue/include/sys_hal.h
 *
 * Low-analog entry/exit used by the vendored PHY/Wi-Fi runtime.
 *
 * Upstream toggles a sequence of analog LDO/buffer bits through sys_ll; that
 * latched analog-register layer is not ported. Implementations in
 * glue/analog_shim.c therefore log loudly and touch no hardware, per the
 * project's "analog writes may not be silent no-ops" rule.
 */

#ifndef __BK7258_WIFI_GLUE_SYS_HAL_H
#define __BK7258_WIFI_GLUE_SYS_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

void sys_hal_enter_low_analog(void);
void sys_hal_exit_low_analog(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_SYS_HAL_H */
