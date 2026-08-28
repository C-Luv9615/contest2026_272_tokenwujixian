/* ioctl_inet(): the transport driver_beken.c uses to reach the WPA/MAC bridge.
 *
 * This file was previously classified as unused in the bk_patch survey. That
 * was wrong: driver_beken.c issues five STA operations through ioctl_inet()
 * (SIOCSIWESSID, SIOCSIWFREQ, SIOCGIWRANGE, PRISM2_IOCTL_PRISM2_PARAM and
 * PRISM2_IOCTL_HOSTAPD), and ioctl_inet() forwards them to hapd_intf_ioctl().
 * Without it the STA driver cannot set an SSID or a channel.
 *
 * It is a small dispatcher, not a socket implementation: it compares the caller
 * socket number against ioctl_get_socket_num() and then calls into the bridge.
 */

#include <bk_prelude.h>

/* Consume the sk_intf/fake_socket guards with the NuttX-safe replacements so
 * the vendored quoted includes cannot re-enter the conflicting bk_patch
 * definitions of struct sockaddr / enum sock_type.
 */

#include <wpa_compat/fake_socket.h>
#include <wpa_compat/sk_intf.h>

#include "driver.h"
#include "../../third_party/beken_armino/wpa_supplicant/bk_patch/ddrv.c"
