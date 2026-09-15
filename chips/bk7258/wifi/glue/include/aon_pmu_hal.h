/*
 * chips/bk7258/wifi/glue/include/aon_pmu_hal.h
 *
 * AON PMU register accessor used by the vendored bk_phy_adapter.c:165
 * (`aon_pmu_hal_get_reg0x7c()` -> `aon_pmu_hal_reg_get(PMU_REG0x7c)`), which is
 * registered into the PHY capability table, so libbk_phy.a can read it.
 *
 * Unlike the other Armino driver headers in this layer, this one is NOT empty:
 * the call is live. The enum names/order are upstream's (sys_types.h:535-545);
 * upstream maps them to AON_PMU_Rxx_ADDR through PMU_ADDRESS_MAP, but that
 * mapping stays inside the accessor, so only the names have to match here.
 *
 * The implementation is a documented stub in glue/hw_driver_shim.c -- the
 * BK7258 AON PMU is not mapped in the team chip layer yet. See there for why
 * that matters before RF bring-up.
 */

#ifndef __BK7258_WIFI_GLUE_AON_PMU_HAL_H
#define __BK7258_WIFI_GLUE_AON_PMU_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  PMU_REG0 = 0,
  PMU_REG1,
  PMU_REG2,
  PMU_REG3,
  PMU_REG0x25,
  PMU_REG0x40,
  PMU_REG0x41,
  PMU_REG0x42,
  PMU_REG0x43,
  PMU_REG0x70,
  PMU_REG0x71,
  PMU_REG0x7c,
  PMU_NONE
} pmu_reg_e;

uint32_t aon_pmu_hal_reg_get(pmu_reg_e reg);
uint32_t aon_pmu_hal_get_chipid(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_AON_PMU_HAL_H */
