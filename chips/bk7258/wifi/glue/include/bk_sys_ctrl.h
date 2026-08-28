/*
 * chips/bk7258/wifi/glue/include/bk_sys_ctrl.h
 *
 * System-control command codes and analog trim masks consumed by the vendored
 * bk_phy_adapter.c, which both passes the CMD_* codes to ddev_control() and puts
 * the masks in the PHY value table (lines 772-773).
 *
 * ==== Analog trim masks: SoC branch matters ====
 *
 * Upstream defines these three times, gated on SoC family
 * (bk_private/bk_sys_ctrl.h:313-325):
 *
 *   BK7231N / BK7236A / BK7256XX     XTALH_CTUNE 0x7F
 *   BK7236XX / BK7239XX / BK7286XX   XTALH_CTUNE 0xFF   <- BK7258 is here
 *   everything else except BK7231    XTALH_CTUNE 0x3F
 *
 * Picking a neighbouring branch compiles and links cleanly, then halves the
 * crystal fine-tune range and skews RF frequency accuracy. AUD_DAC_GAIN_MASK is
 * 0x1F in all three; stated explicitly so that is visibly a fact, not an
 * assumption.
 *
 * ==== CMD_* codes: positional inside a magic-anchored enum ====
 *
 * The commands are enum members anchored at SCTRL_CMD_MAGIC (0xC123000), so their
 * values fall out of the member order rather than being written down. The enum
 * was walked to derive the ones used here; they are spelled as explicit constants
 * because a trimmed enum would renumber silently.
 */

#ifndef __BK7258_WIFI_GLUE_BK_SYS_CTRL_H
#define __BK7258_WIFI_GLUE_BK_SYS_CTRL_H

#include <common/bk_typedef.h>

/* bk_phy_adapter.c uses CMD_TL410_CLK_PWR_UP and PWD_BLE_CLK_BIT without
 * including bk_icu.h itself, so upstream must be supplying them transitively --
 * its own bk_sys_ctrl.h pulls in "pmu.h", which we do not vendor. Hooking
 * bk_icu.h in here reproduces that reachability through a chain we own.
 */

#include "bk_icu.h"

#define SCTRL_CMD_MAGIC             (0xC123000)
#define SCTRL_FAILURE               ((UINT32)-1)
#define SCTRL_SUCCESS               (0)

/* Analog trim masks, BK7236XX family. */

#define PARAM_XTALH_CTUNE_MASK      (0xFF)
#define PARAM_AUD_DAC_GAIN_MASK     (0x1F)

/* ddev_control command codes, derived by walking the SCTRL enum. */

#define CMD_GET_CHIP_ID             (0x0C123001)
#define CMD_GET_DEVICE_ID           (0x0C123002)
#define CMD_SCTRL_BLE_POWERDOWN     (0x0C12301B)
#define CMD_SCTRL_BLE_POWERUP       (0x0C12301C)
#define CMD_BLE_RF_BIT_SET          (0x0C123034)
#define CMD_BLE_RF_BIT_CLR          (0x0C123035)
#define CMD_SCTRL_SET_VDD_VALUE     (0x0C123070)
#define CMD_SCTRL_GET_VDD_VALUE     (0x0C123071)

#define SYS_DRV_CLK_ON              1
#define SYS_DRV_CLK_OFF             0
#define PM_CHIP_ID_MASK             0xFFFFFFFFu
#define PM_CHIP_ID_MPW_V2_3         0x22710010u
#define PM_CHIP_ID_MPW_V4           0x22C20010u
#define PM_CHIP_ID_MP_A             0x23640810u
#define SARADC_AUTOTEST             0

#endif /* __BK7258_WIFI_GLUE_BK_SYS_CTRL_H */
