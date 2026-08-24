/*
 * chips/bk7258/wifi/glue/include/sys_ll.h
 *
 * NuttX reimplementation of the Armino low-level SYS register accessors used
 * by the vendored glue (clock-enable state, OFDM sleep/power). Maps onto the
 * team chip-layer register bits in bk7258_memorymap.h.
 */

#ifndef __BK7258_WIFI_GLUE_SYS_LL_H
#define __BK7258_WIFI_GLUE_SYS_LL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t sys_ll_get_cpu_device_clk_enable_mac_cken(void);
uint32_t sys_ll_get_cpu_device_clk_enable_phy_cken(void);

void     sys_ll_set_cpu_power_sleep_wakeup_pwd_ofdm(uint32_t v);
uint32_t sys_ll_get_cpu_power_sleep_wakeup_pwd_ofdm(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_SYS_LL_H */
