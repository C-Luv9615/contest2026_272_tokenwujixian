/****************************************************************************
 * app/wifi_ctrl/wifi_ctrl_main.c
 *
 * NSH front end for the cross-core Wi-Fi control plane.  It only formats
 * what wifi_ctrl.c returns, so the UI can call the same functions directly
 * and get the same data.
 *
 *   wifi_ctrl scan                 start a scan and print the list
 *   wifi_ctrl connect <ssid> <pw>  associate (WPA2/WPA3 passphrase)
 *   wifi_ctrl disconnect           drop the link
 *   wifi_ctrl help
 *
 * The password is never printed.
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wlan_ctrl.h"

static void wifi_ctrl_showusage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s scan\n", progname);
  printf("  %s connect <ssid> <passphrase>\n", progname);
  printf("  %s disconnect\n", progname);
  printf("  %s help\n", progname);
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

  printf("connecting to %s (watch for the address on wlan0)\n", ssid);
  return 0;
}

int main(int argc, FAR char *argv[])
{
  if (argc < 2)
    {
      wifi_ctrl_showusage(argv[0]);
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "scan") == 0)
    {
      return do_scan();
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      if (argc < 4)
        {
          wifi_ctrl_showusage(argv[0]);
          return EXIT_FAILURE;
        }

      return do_connect(argv[2], argv[3]);
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

  wifi_ctrl_showusage(argv[0]);
  return EXIT_FAILURE;
}
