/*
 * chips/bk7258/wifi/glue/include/driver/otp.h
 *
 * OTP item ids and read/update entry points. bk_phy_adapter.c puts the ids in
 * the PHY value table (lines 794-798) and registers the accessors in the
 * function table, so libbk_phy.a uses both to fetch fused calibration data.
 *
 * The ids are NOT in any checked-in header: upstream generates them per build,
 * at build/bk7258/beken_genie/bk7258/armino/partitions/_build/_otp.h. Values
 * below were computed by walking those generated enums, and otp1_id_t /
 * otp2_id_t agree on every id used here (MAC_ADDRESS 26, VDDDIG_BANDGAP 27,
 * DIA 28, SDMADC_CALIBRATION 30, GADC_TEMPERATURE 33); OTP_RFCALI1 (36) exists
 * only in otp2_id_t, which is the AHB variant bk_phy_adapter.c:159 uses.
 *
 * Both id types are uint32_t here rather than two enums: the ids are shared and
 * the API distinction (apb vs ahb) is carried by the function name.
 *
 * Since these come from a *generated* file, they are tied to the genie OTP
 * layout. If this team repo ever defines its own OTP map, these must be
 * re-derived -- reading the wrong item id yields plausible-looking but wrong
 * calibration bytes.
 */

#ifndef __BK7258_WIFI_GLUE_DRIVER_OTP_H
#define __BK7258_WIFI_GLUE_DRIVER_OTP_H

#include <stdint.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t otp1_id_t;
typedef uint32_t otp2_id_t;

#define OTP_MAC_ADDRESS         26
#define OTP_VDDDIG_BANDGAP      27
#define OTP_DIA                 28
#define OTP_SDMADC_CALIBRATION  30
#define OTP_GADC_TEMPERATURE    33
#define OTP_RFCALI1             36

bk_err_t bk_otp_apb_read(otp1_id_t item, uint8_t *buf, uint32_t size);
bk_err_t bk_otp_ahb_read(otp2_id_t item, uint8_t *buf, uint32_t size);
bk_err_t bk_otp_ahb_update(otp2_id_t item, uint8_t *buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_DRIVER_OTP_H */
