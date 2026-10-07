/****************************************************************************
 * chips/bk7258/wlan_ctrl/wlan_ctrl_cli.c
 *
 * NSH front end for the cross-core Wi-Fi control plane, named `wapi` and
 * taking the same arguments as the NuttX wapi tool on CP so that switching
 * consoles does not mean switching habits:
 *
 *   wapi scan [ifname]                  scan and print the list
 *   wapi scan_results [ifname]          print the last list again
 *   wapi psk <ifname> <pass> [alg]      set the passphrase
 *   wapi essid <ifname> <ssid> [flag]   set the ssid; flag 1 connects
 *   wapi connect <ssid> <pass>          both of the above in one go
 *   wapi disconnect [ifname]            drop the link
 *
 * The ifname is accepted and ignored: the only station is wlan0 and it
 * lives on CP.  This is the rpmsg face; CP's own `wapi` is the wext face
 * of the same operations (compat/bk7258_armino_wops.c), and both end up
 * in chips/bk7258/wlan_ctrl/wlan_ctrl_server.c, so they share one job
 * queue and cannot race into the Armino connect twice.
 *
 * The password is never printed.
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wlan_ctrl.h"

static char s_ssid[WLAN_CTRL_SSID_LEN];
static char s_pass[WLAN_CTRL_PASS_LEN];

static void wifi_showusage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s scan [ifname]\n", progname);
  printf("  %s scan_results [ifname]\n", progname);
  printf("  %s psk <ifname> <passphrase> [alg]\n", progname);
  printf("  %s essid <ifname> <ssid> [flag]\n", progname);
  printf("  %s connect <ssid> <passphrase>\n", progname);
  printf("  %s disconnect [ifname]\n", progname);
  printf("\n");
  printf("  essid flag: 0 off, 1 on (connects), 2 delayed\n");
  printf("  psk alg:    0 none, 1 wep, 2 tkip, 3 ccmp\n");
}

static int do_scan(void)
{
  struct wifi_ap_info_s aps[32];
  int n = 0;
  int ret;
  int i;

  ret = wifi_ctrl_scan_start();
  if (ret < 0)
    {
      printf("scan start failed: %d\n", ret);
      return 1;
    }

  /* Armino's scan is asynchronous; poll until results show up. */

  for (i = 0; i < 50; i++)
    {
      ret = wifi_ctrl_scan_get(aps, 32, &n);
      if (ret < 0)
        {
          printf("scan get failed: %d\n", ret);
          return 1;
        }

      if (n > 0)
        {
          break;
        }

      usleep(100 * 1000);
    }

  printf("bssid                ch  rssi  sec  ssid\n");
  for (i = 0; i < n; i++)
    {
      printf("%02x:%02x:%02x:%02x:%02x:%02x  %3d  %4d  %3s  %s\n",
             aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
             aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5],
             aps[i].channel, aps[i].rssi,
             aps[i].secured ? "yes" : "no", aps[i].ssid);
    }

  if (n == 0)
    {
      printf("(no APs)\n");
    }

  return 0;
}

static int do_connect(FAR const char *ssid, FAR const char *pass)
{
  int ret = wifi_ctrl_connect(ssid, pass);

  if (ret < 0)
    {
      printf("connect failed: %d\n", ret);
      return 1;
    }

  printf("connecting to %s\n", ssid);
  return 0;
}

/* wapi psk / wapi essid arrive as two separate commands; hold the halves
 * between them and connect when essid's flag is 1, which is what
 * netdev_upper_wireless_ioctl() does on the wext face.
 */

int main(int argc, FAR char *argv[])
{
  if (argc < 2)
    {
      wifi_showusage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "scan") == 0 || strcmp(argv[1], "scan_results") == 0)
    {
      return do_scan();
    }

  if (strcmp(argv[1], "psk") == 0)
    {
      FAR const char *pass = (argc >= 4) ? argv[3] : (argc >= 3 ? argv[2] : NULL);

      if (pass == NULL)
        {
          wifi_showusage(argv[0]);
          return EXIT_FAILURE;
        }

      strlcpy(s_pass, pass, sizeof(s_pass));
      return 0;
    }

  if (strcmp(argv[1], "essid") == 0)
    {
      FAR const char *ssid = (argc >= 4) ? argv[3] : (argc >= 3 ? argv[2] : NULL);
      int flag = (argc >= 5) ? atoi(argv[4]) : 1;

      if (ssid == NULL)
        {
          wifi_showusage(argv[0]);
          return EXIT_FAILURE;
        }

      strlcpy(s_ssid, ssid, sizeof(s_ssid));
      if (flag == 1)
        {
          return do_connect(s_ssid, s_pass);
        }

      return 0;
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      if (argc < 4)
        {
          wifi_showusage(argv[0]);
          return EXIT_FAILURE;
        }

      strlcpy(s_ssid, argv[2], sizeof(s_ssid));
      strlcpy(s_pass, argv[3], sizeof(s_pass));
      return do_connect(s_ssid, s_pass);
    }

  if (strcmp(argv[1], "disconnect") == 0)
    {
      int ret = wifi_ctrl_disconnect();

      if (ret < 0)
        {
          printf("disconnect failed: %d\n", ret);
          return 1;
        }

      printf("disconnecting\n");
      return 0;
    }

  wifi_showusage(argv[0]);
  return EXIT_FAILURE;
}
