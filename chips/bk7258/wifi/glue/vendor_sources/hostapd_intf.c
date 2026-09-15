/* The WPA <-> MAC ioctl bridge.
 *
 * Despite the "hostapd"/"hapd_intf" naming this is not a SoftAP-only unit: it
 * is the single bridge every WPA request crosses on its way to the vendor MAC.
 * The STA path needs it for
 *
 *   wpa_send_scan_req / wpa_get_scan_rst / wpa_clear_scan_results
 *   wpa_send_auth_req / wpa_send_assoc_req / wpa_send_disconnect_req
 *   hapd_intf_add_key / hapd_intf_del_key / hapd_intf_add_vif
 *   hapd_intf_ioctl   (reached from driver_beken.c through ddrv.c:ioctl_inet)
 *   hapd_intf_ke_rx_handle (MLME/EAPOL TX out of the WPA socket queue)
 *
 * Beken shares one queue and one handler between wpa_supplicant and hostapd,
 * so guarding this file by CONFIG_AP would cut the STA authentication and
 * association frames, not just SoftAP. The AP-only entry points it also
 * contains (start/stop APM, beacon, sta_add/del) reach the MAC through
 * rw_msg_tx.c and are simply never invoked while SoftAP stays disabled.
 */

#include <bk_prelude.h>

/* Claim the sk_intf/fake_socket guards with the NuttX-safe replacements before
 * the vendored source's quoted includes can pull in the adjacent bk_patch
 * headers, whose struct sockaddr / enum sock_type collide with NuttX.
 */

#include <wpa_compat/fake_socket.h>
#include <wpa_compat/sk_intf.h>

#include "driver.h"
#include "../../third_party/beken_armino/glue/bk_wifi/src/hostapd_intf.c"
