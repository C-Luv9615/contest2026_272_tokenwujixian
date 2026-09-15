/*
 * chips/bk7258/wifi/glue/include/sys_driver.h
 *
 * NuttX reimplementation of the Armino sys_ctrl driver callbacks the vendored
 * glue registers into the Wi-Fi driver capability table. Only the interrupt
 * and modem-clock subset used by rw_task.c / bk_wifi_adapter.c is declared;
 * implementations are in hw_driver_shim.c and map onto the team chip layer.
 */

#ifndef __BK7258_WIFI_GLUE_SYS_DRIVER_H
#define __BK7258_WIFI_GLUE_SYS_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_include.h>
#include <components/log.h>
#include <modules/pm.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t sys_drv_int_disable(uint32_t param);
int32_t sys_drv_int_enable(uint32_t param);
int32_t sys_drv_int_group2_disable(uint32_t param);
int32_t sys_drv_int_group2_enable(uint32_t param);

uint32_t sys_drv_modem_bus_clk_ctrl(bool clk_en);
uint32_t sys_drv_modem_clk_ctrl(bool clk_en);

int32_t sys_drv_module_power_state_get(power_module_name_t module);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_SYS_DRIVER_H */
