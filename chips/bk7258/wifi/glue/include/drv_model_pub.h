/*
 * chips/bk7258/wifi/glue/include/drv_model_pub.h
 *
 * Armino's legacy driver-model handles. bk_phy_adapter.c reaches the SCTRL / ICU
 * / BLE blocks through ddev_open()+ddev_control() rather than a typed API, and
 * puts the device-type ids in the PHY value table.
 *
 * The ids are positional inside one long enum anchored at
 * DD_HANDLE_MAGIC_WORD - 1 (0xA5A4FFFF), so they cannot be read off individual
 * lines. The whole 38-member enum was walked to derive the ones used here:
 *
 *     DD_DEV_TYPE_BLE     0xA5A50001
 *     DD_DEV_TYPE_ICU     0xA5A50006
 *     DD_DEV_TYPE_SCTRL   0xA5A5000E
 *     DD_DEV_TYPE_FLASH   0xA5A50015
 *     DD_DEV_TYPE_SARADC  0xA5A50021
 *     DD_DEV_TYPE_RF      0xA5A50022
 *
 * They are written as explicit constants, not as a trimmed enum: an enum missing
 * members would renumber silently, which is the whole hazard here.
 *
 * ddev_open/close/read/write are already declared in bk_drv_model.h; only
 * ddev_control is added, since that is what bk_phy_adapter.c uses. The
 * implementation is in glue/analog_shim.c and fails rather than pretending.
 */

#ifndef __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H
#define __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H

#include <stdint.h>

#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DD_HANDLE_MAGIC_WORD    (0xA5A50000)

#define DD_DEV_TYPE_NONE        (0x00000000)
#define DD_DEV_TYPE_BLE         (0xA5A50001)
#define DD_DEV_TYPE_ICU         (0xA5A50006)
#define DD_DEV_TYPE_SCTRL       (0xA5A5000E)
#define DD_DEV_TYPE_FLASH       (0xA5A50015)
#define DD_DEV_TYPE_SARADC      (0xA5A50021)
#define DD_DEV_TYPE_RF          (0xA5A50022)

typedef UINT32 DD_HANDLE;
typedef UINT32 dd_device_type;

DD_HANDLE ddev_open(dd_device_type dev, UINT32 *status, UINT32 op_flag);
UINT32    ddev_close(DD_HANDLE handle);
UINT32    ddev_read(DD_HANDLE handle, char *user_buf, UINT32 count,
                    UINT32 op_flag);
UINT32    ddev_write(DD_HANDLE handle, char *user_buf, UINT32 count,
                     UINT32 op_flag);
UINT32    ddev_control(DD_HANDLE handle, UINT32 cmd, void *param);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRV_MODEL_PUB_H */
