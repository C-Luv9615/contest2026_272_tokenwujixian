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

/* BK7258 analog fields used by the PHY adapter.  Each implementation is in
 * analog_shim.c and uses the chip layer's locked, latched transaction rather
 * than a direct SYS MMIO store. */
void     sys_ll_set_ana_reg5_adc_div(uint32_t v);
uint32_t sys_ll_get_ana_reg5_adc_div(void);
void     sys_ll_set_ana_reg8_ioldo_lp(uint32_t v);
void     sys_ll_set_ana_reg8_iocurlim(uint32_t v);
void     sys_ll_set_ana_reg9_vcorehsel(uint32_t v);
void     sys_ll_set_ana_reg9_spi_latch1v(uint32_t v);
void     sys_ll_set_ana_reg10_iobyapssen(uint32_t v);
void     sys_ll_set_ana_reg11_aldosel(uint32_t v);
void     sys_ll_set_ana_reg12_dldosel(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_SYS_LL_H */
