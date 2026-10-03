/****************************************************************************
 * chips/bk7258/wifi_armino/bk7258_armino_sta.c
 *
 * NSH command `armino_sta <ssid> [password]` for the phase-one build
 * (docs/compose/spec/bk7258-wifi-native.md, S2.9). It connects through
 * Armino's own entry, demo_sta_app_init() (bk_wifi/src/wifi_api.c:183),
 * which is bk_wifi_sta_set_config() + bk_wifi_sta_start(), the sequence
 * a7de5d2 connected with on the board. Temporary: the connection entry is
 * to become wlan0's wireless ops, so that wapi drives it.
 *
 * The address then follows the baseline flow: EVENT_WIFI_STA_CONNECTED ->
 * sta_ip_start() -> DHCP -> "[ARMINO] wlan0 ip ...".
 *
 * demo_sta_app_init() copies both strings with os_strcpy() into 33- and
 * 65-byte fields, so the lengths are checked here first. The password is
 * never printed.
 *
 * `armino_sta disconnect` runs bk_wifi_sta_disconnect() instead, so the
 * disconnect/reconnect path (and the stale-address guard in sta_ip_down())
 * can be exercised without a reboot.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#define ARMINO_SSID_MAX      32   /* WIFI_SSID_STR_LEN - 1 */
#define ARMINO_PASSWORD_MIN  8    /* WPA2 passphrase */
#define ARMINO_PASSWORD_MAX  64   /* WIFI_PASSWORD_LEN - 1, 64 hex = PMK */

/* bk_private/bk_wifi.h:503; CONFIG_CONNECT_THROUGH_PSK_OR_SAE_PASSWORD is
 * not set in the baseline sdkconfig, so this is the two-argument form.
 */

int demo_sta_app_init(char *oob_ssid, char *connect_key);

/* wifi.h:103; drops the link without freeing the STA, so a reconnect does
 * not need a full stop/start.
 */

int bk_wifi_sta_disconnect(void);

int main(int argc, char *argv[])
{
  char *password = argc > 2 ? argv[2] : "";
  size_t ssid_len;
  size_t password_len;

  if (argc == 2 && strcmp(argv[1], "disconnect") == 0)
    {
      printf("disconnecting\n");
      bk_wifi_sta_disconnect();
      return 0;
    }

  if (argc < 2 || argc > 3)
    {
      printf("usage: %s <ssid> [password]\n", argv[0]);
      printf("       %s disconnect\n", argv[0]);
      return 1;
    }

  ssid_len = strlen(argv[1]);
  password_len = strlen(password);
  if (ssid_len == 0 || ssid_len > ARMINO_SSID_MAX)
    {
      printf("ssid must be 1..%d bytes\n", ARMINO_SSID_MAX);
      return 1;
    }

  if (password_len != 0 &&
      (password_len < ARMINO_PASSWORD_MIN ||
       password_len > ARMINO_PASSWORD_MAX))
    {
      printf("password must be empty (open) or %d..%d characters\n",
             ARMINO_PASSWORD_MIN, ARMINO_PASSWORD_MAX);
      return 1;
    }

  if (password_len == 0)
    {
      printf("connecting to \"%s\", open\n", argv[1]);
    }
  else
    {
      printf("connecting to \"%s\", password %u chars\n", argv[1],
             (unsigned int)password_len);
    }

  /* Failures are logged by Armino (BK_LOG_ON_ERR); progress shows in the
   * wpa/wifid logs and, once associated, in "[ARMINO] wlan0 ip".
   */

  demo_sta_app_init(argv[1], password);
  return 0;
}
