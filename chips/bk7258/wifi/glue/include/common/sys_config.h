/*
 * chips/bk7258/wifi/glue/include/common/sys_config.h
 *
 * NuttX reimplementation of the Armino build-config header. Maps the Armino
 * CONFIG_* feature macros onto the BK7258 NuttX adaptation defaults. Defaults
 * reflect the STA MVP (plan §11): Wi-Fi enabled, no lwIP, no FreeRTOS, no
 * P2P/bridge/virtual-controller. Values are refined as the vendored sources
 * are compiled against NuttX.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H
#define __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H

#include <nuttx/config.h>

/* SoC family switches: BK7258 is none of the BK7236xx/BK7239xx/BK7256xx/
 * BK7286xx families these conditionals gate. */
#define CONFIG_SOC_BK7236XX         0
#define CONFIG_SOC_BK7239XX         0
#define CONFIG_SOC_BK7256XX         0
#define CONFIG_SOC_BK7286XX         0

/* Runtime/hosting model: MAC/PHY runs on this core (NO_HOSTED), NuttX owns
 * the network stack (no lwIP), no second scheduler (no FreeRTOS SMP). */
#define CONFIG_NO_HOSTED            1
#define CONFIG_FULLY_HOSTED         0
#define CONFIG_SEMI_HOSTED          0
#define CONFIG_LWIP                 0
#define CONFIG_FREERTOS_SMP         0

/* Wi-Fi core. */
#define CONFIG_WIFI_ENABLE          1
#define CONFIG_WIFI6                1
#define CONFIG_WIFI4                0
#define CONFIG_WIFI_BAND_5G         0
#define CONFIG_WAPI_SUPPORT         0
#define CONFIG_BLUETOOTH            0

/* STA MVP keeps these off. */
#define CONFIG_WIFI_VNET_CONTROLLER 0
#define CONFIG_P2P                  0
#define CONFIG_BRIDGE               0
#define CONFIG_STA_AUTO_RECONNECT   0
#define CONFIG_MONITOR_REQ          0
#define CONFIG_ROLE_AP              0
#define CONFIG_ROLE_STA             1

/* Debug/trace off at skeleton stage. */
#define CONFIG_RWNX_PROTO_DEBUG     0
#define CONFIG_SHELL_ASYNCLOG       0

#endif /* __BK7258_WIFI_GLUE_COMMON_SYS_CONFIG_H */
