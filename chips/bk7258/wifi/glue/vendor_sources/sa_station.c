/* STA association command provider.
 *
 * hostapd_intf.c:974 (wpa_send_assoc_req) calls
 * sa_station_send_associate_cmd() to hand the association parameters to the
 * vendor MAC, so this is part of the STA connect path, not an AP unit.
 *
 * CONFIG_SME is not defined in this profile, so sa_station.c compiles its
 * `#else` variant taking CONNECT_PARAM_T. That matches the caller at
 * hostapd_intf.c:974, which passes `connect_param`; the CONFIG_SME variant
 * (ASSOC_PARAM_T, hostapd_intf.c:896) is compiled out on both sides.
 */

#include <bk_prelude.h>

/* Cut the auto-reconnect thread at the vendor's own configuration boundary.
 *
 * That block needs CONFIG_TASK_RECONNECT_PRIO, an Armino task priority with no
 * NuttX equivalent (the SDK sets it to 4 in bk7258.defconfig, which is a
 * FreeRTOS priority number and does not carry over). More importantly the block
 * is vendor dead code: sa_reconnect_init() begins with `return; // try it;`, so
 * the thread is never created regardless of the priority.
 *
 * Defining a NuttX priority for a thread that cannot start would be inventing a
 * contract, so use DISABLE_RECONNECT instead. Nothing in the compiled set calls
 * sa_reconnect_init(); the only other reference is a prototype in
 * bk_private/bk_wifi_types.h, and an uncalled declaration needs no definition.
 * Reconnect policy belongs to the NuttX network manager on this port anyway.
 */

#define DISABLE_RECONNECT 1

#include "driver.h"
#include "../../third_party/beken_armino/glue/bk_wifi/src/sa_station.c"
