/* Monitor callback registry and management-frame filter.
 *
 * rwnx_rx.c needs two symbols from this unit on the normal STA RX path:
 *
 *   wifi_monitor_get_cb()  rwnx_rx.c:729 - returns the (normally NULL) monitor
 *                          callback, which is how the RX path decides that a
 *                          frame is not destined for a monitor interface.
 *   rx_mgmt_filter()       called by libwifi.a's rxl_cntrl to decide whether a
 *                          management frame is discarded before it reaches the
 *                          upper layers.
 *
 * Both sit outside this file's SUPPORT_LSIG_MONITOR guards (lines 152-199 and
 * 285-289). That macro is undefined in this profile, so the L-SIG monitor
 * machinery itself is compiled out while these two entry points remain, which
 * is exactly the STA-only subset we want.
 */

#include <bk_prelude.h>

/* Claim the NuttX-safe socket/sk_intf replacements before the vendored quoted
 * includes (driver_beken.h, main_none.h) can reach the bk_patch headers whose
 * struct sockaddr / enum sock_type collide with NuttX.
 */

#include <wpa_compat/fake_socket.h>
#include <wpa_compat/sk_intf.h>

#include "driver.h"
#include "../../third_party/beken_armino/glue/bk_wifi/src/lsig_monitor.c"
