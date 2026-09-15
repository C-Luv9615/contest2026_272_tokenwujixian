/*
 * chips/bk7258/wifi/glue/include/aon_pmu_driver.h
 *
 * AON PMU calibration accessors used by the vendored bk_phy_adapter.c, which
 * wraps them into the PHY capability table so libbk_phy.a can call them.
 *
 * Only the two the compiled set actually references are declared:
 * aon_pmu_drv_get_adc_cal() and aon_pmu_drv_bias_cal_get(). Upstream's header
 * also has the sleep/wakeup/touch accessors; those belong to power management,
 * which is out of STA MVP scope.
 *
 * Implementations are documented stubs in glue/hw_driver_shim.c -- the BK7258
 * AON PMU is not mapped in the team chip layer yet, and these feed RF
 * calibration, so see the note there before RF bring-up.
 */

#ifndef __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H
#define __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t aon_pmu_drv_get_adc_cal(void);
uint32_t aon_pmu_drv_bias_cal_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_AON_PMU_DRIVER_H */
