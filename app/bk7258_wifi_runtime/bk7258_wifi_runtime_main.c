#include <nuttx/config.h>
#include <nuttx/kthread.h>
#include <nuttx/wireless/wireless.h>

#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include "../../chips/bk7258/wifi/bk7258_wifi_internal.h"

struct bk7258_scan_print_s
{
  bool have_ap;
  bool protected;
  char ssid[IW_ESSID_MAX_SIZE + 1];
  uint8_t bssid[6];
  int8_t rssi;
  uint16_t channel;
};

/* The NSH application is configured with a 4 KiB stack. Keep the maximum
 * WEXT result buffer in static storage so a scan cannot consume that whole
 * stack before the ioctl/formatting frames are accounted for. The lower-half
 * serializes scans, so concurrent scan commands fail at start with -EBUSY and
 * cannot race this buffer. */

static uint8_t g_bk7258_scan_results[IW_SCAN_MAX_DATA];

#define BK7258_WIFI_APP_PRIORITY  85
#define BK7258_WIFI_APP_STACKSIZE 8192

static void bk7258_wifi_scan_print(const struct bk7258_scan_print_s *ap)
{
  if (!ap->have_ap)
    {
      return;
    }

  printf("  %02x:%02x:%02x:%02x:%02x:%02x  ch %-2u  %4d dBm  %-9s %s\n",
         ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3],
         ap->bssid[4], ap->bssid[5], ap->channel, ap->rssi,
         ap->protected ? "protected" : "open", ap->ssid);
}

static void bk7258_wifi_scan_decode(const uint8_t *data, size_t length)
{
  struct bk7258_scan_print_s ap = {0};
  size_t offset = 0;

  printf("BSSID              channel RSSI      security  SSID\n");
  while (offset + offsetof(struct iw_event, u) <= length)
    {
      const struct iw_event *iwe =
        (const struct iw_event *)(data + offset);
      size_t payload_length;

      if (iwe->len < offsetof(struct iw_event, u) ||
          iwe->len > length - offset)
        {
          printf("[bk7258_wifi_runtime] malformed scan result at byte %lu\n",
                 (unsigned long)offset);
          return;
        }

      payload_length = iwe->len - offsetof(struct iw_event, u);
      if (iwe->cmd == SIOCGIWAP && iwe->len >= IW_EV_LEN(ap_addr))
        {
          bk7258_wifi_scan_print(&ap);
          memset(&ap, 0, sizeof(ap));
          ap.have_ap = true;
          memcpy(ap.bssid, iwe->u.ap_addr.sa_data, sizeof(ap.bssid));
        }
      else if (iwe->cmd == SIOCGIWESSID && iwe->len >= IW_EV_LEN(essid))
        {
          size_t ssid_length = iwe->u.essid.length;
          size_t available = payload_length - sizeof(iwe->u.essid);

          if (ssid_length > IW_ESSID_MAX_SIZE)
            {
              ssid_length = IW_ESSID_MAX_SIZE;
            }
          if (ssid_length > available)
            {
              ssid_length = available;
            }

          memcpy(ap.ssid, &iwe->u.essid + 1, ssid_length);
          ap.ssid[ssid_length] = '\0';
        }
      else if (iwe->cmd == IWEVQUAL && iwe->len >= IW_EV_LEN(qual))
        {
          ap.rssi = (int8_t)iwe->u.qual.level;
        }
      else if (iwe->cmd == SIOCGIWFREQ && iwe->len >= IW_EV_LEN(freq))
        {
          ap.channel = iwe->u.freq.e == 0 ? iwe->u.freq.m : 0;
        }
      else if (iwe->cmd == SIOCGIWENCODE && iwe->len >= IW_EV_LEN(data))
        {
          ap.protected = (iwe->u.data.flags & IW_ENCODE_DISABLED) == 0;
          bk7258_wifi_scan_print(&ap);
          memset(&ap, 0, sizeof(ap));
        }

      offset += iwe->len;
    }

  bk7258_wifi_scan_print(&ap);
}

static int bk7258_wifi_runtime_scan(void)
{
  struct iwreq request = {0};
  int socket_fd;
  int ret;
  unsigned int attempts;

  socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0)
    {
      printf("[bk7258_wifi_runtime] scan: socket failed: %d\n", errno);
      return 1;
    }

  strlcpy(request.ifr_name, "wlan0", sizeof(request.ifr_name));
  ret = ioctl(socket_fd, SIOCSIWSCAN, (unsigned long)&request);
  if (ret < 0)
    {
      printf("[bk7258_wifi_runtime] scan: start failed: %d\n", errno);
      close(socket_fd);
      return 1;
    }

  for (attempts = 0; attempts < 100; attempts++)
    {
      memset(&request.u.data, 0, sizeof(request.u.data));
      request.u.data.pointer = g_bk7258_scan_results;
      request.u.data.length = sizeof(g_bk7258_scan_results);
      ret = ioctl(socket_fd, SIOCGIWSCAN, (unsigned long)&request);
      if (ret == 0)
        {
          printf("[bk7258_wifi_runtime] scan: %u result bytes\n",
                 request.u.data.length);
          bk7258_wifi_scan_decode(g_bk7258_scan_results,
                                  request.u.data.length);
          close(socket_fd);
          return 0;
        }

      if (errno != EAGAIN)
        {
          printf("[bk7258_wifi_runtime] scan: result failed: %d\n", errno);
          close(socket_fd);
          return 1;
        }

      usleep(100 * 1000);
    }

  printf("[bk7258_wifi_runtime] scan: timed out waiting for completion\n");
  close(socket_fd);
  return 1;
}

static int bk7258_wifi_runtime_worker(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;
  syslog(LOG_INFO, "[BK7258] Wi-Fi application init starting\n");
  ret = bk7258_wifi_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] Wi-Fi application init failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "[BK7258] Wi-Fi application init completed\n");
    }

  return ret;
}

static int bk7258_wifi_runtime_init(void)
{
  int pid;

  pid = kthread_create("bk7258-wifi", BK7258_WIFI_APP_PRIORITY,
                       BK7258_WIFI_APP_STACKSIZE,
                       bk7258_wifi_runtime_worker, NULL);
  if (pid < 0)
    {
      syslog(LOG_ERR, "[BK7258] Wi-Fi application thread failed: %d\n", pid);
      return pid;
    }

  syslog(LOG_INFO, "[BK7258] Wi-Fi runtime task started pid=%d\n", pid);
  return 0;
}

int main(int argc, char **argv)
{
  if (argc != 2)
    {
      printf("usage: bk7258_wifi_runtime init|scan\n");
      return 1;
    }

  if (strcmp(argv[1], "init") == 0)
    {
      return bk7258_wifi_runtime_init();
    }

  if (strcmp(argv[1], "scan") == 0)
    {
      /* A bare scan must not run against an unpowered MAC/PHY domain: the
       * team hw_init (power gates + clocks) and bk_wifi_init() only run
       * inside bk7258_wifi_initialize().  That sequence needs its own large
       * stack (the vendor init chain nests deeply), so reuse the exact
       * kthread path the init command uses and wait for completion here
       * instead of running it on this NSH thread. */
      if (!bk7258_wifi_is_ready())
        {
          int pid = kthread_create("bk7258-wifi", BK7258_WIFI_APP_PRIORITY,
                                   BK7258_WIFI_APP_STACKSIZE,
                                   bk7258_wifi_runtime_worker, NULL);

          if (pid < 0)
            {
              printf("[bk7258_wifi_runtime] scan: init thread failed: %d\n",
                     pid);
              return 1;
            }

          for (int i = 0; i < 300 && !bk7258_wifi_is_ready(); i++)
            {
              usleep(100 * 1000);
            }

          if (!bk7258_wifi_is_ready())
            {
              printf("[bk7258_wifi_runtime] scan: init did not complete; "
                     "see syslog above\n");
              return 1;
            }
        }

      return bk7258_wifi_runtime_scan();
    }

  printf("usage: bk7258_wifi_runtime init|scan\n");
  return 1;
}
