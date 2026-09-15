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

static int bk7258_wifi_runtime_scan(FAR const char *ssid)
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

  /* Select the SSID the next scan probes for -- ALWAYS issued, including the
   * broadcast case, because the driver keeps this selection across scans: a
   * bare `scan` after a `scan <ssid>` would otherwise stay directed and the
   * reading would be misattributed.  A NULL pointer clears it.
   *
   * IW_ESSID_DELAY_ON, never IW_ESSID_ON.  netdev_upperhalf.c:1017 calls
   * ops->connect() right after a successful essid set when the flag is
   * IW_ESSID_ON, and associating before a scan has found an AP is exactly
   * what the STA-only bring-up contract forbids.  DELAY_ON records the SSID
   * and stops there.  IW_ESSID_OFF is avoided too: the upper half routes it
   * to its disconnect path rather than to ops->essid.  No credentials are
   * involved either way. */

  request.u.essid.pointer = (FAR void *)ssid;
  request.u.essid.length = ssid != NULL ? strlen(ssid) : 0;
  request.u.essid.flags = IW_ESSID_DELAY_ON;

  ret = ioctl(socket_fd, SIOCSIWESSID, (unsigned long)&request);
  if (ret < 0)
    {
      printf("[bk7258_wifi_runtime] scan: essid failed: %d\n", errno);
      close(socket_fd);
      return 1;
    }

  memset(&request.u, 0, sizeof(request.u));

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

/* Runs the association request on its own thread.
 *
 * The vendor connect chain (bk_wifi_sta_start -> wlan_sta_set ->
 * wpa_psk_request -> bk_wifi_sta_connect) nests about as deeply as init does,
 * and this NSH application only gets the 4 KiB stack its CMakeLists asks for,
 * so the request cannot be issued from main() -- same reasoning the scan
 * command already documents for the init sequence below.
 *
 * ssid and psk arrive through argv because NuttX copies the argument strings
 * onto the new task's stack (nxtask_setup_stackargs), so they stay valid after
 * main() moves on, and they are gone once this thread exits -- no module-level
 * variable holds the passphrase for the lifetime of the process.
 */

static int bk7258_wifi_connect_worker(int argc, char *argv[])
{
  int ret;

  /* argv[0] is the task name; the two credentials follow it. */

  if (argc < 3 || argv[1] == NULL || argv[2] == NULL)
    {
      syslog(LOG_ERR, "[BK7258] connect worker: missing arguments\n");
      return -EINVAL;
    }

  ret = bk7258_wifi_sta_connect(argv[1], argv[2]);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[BK7258] connect request rejected: %d\n", ret);
    }

  return ret;
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

/* Bring the vendor stack up unless it already is, and wait for it.
 *
 * Extracted from the scan command, which needed exactly this and now shares
 * it with connect: the team hw_init (power gates + clocks) and bk_wifi_init()
 * only run inside bk7258_wifi_initialize(), and a bare scan or connect must
 * not run against an unpowered MAC/PHY domain.  That sequence needs its own
 * large stack (the vendor init chain nests deeply), so it goes on the same
 * kthread the init command uses rather than on this NSH thread.
 */

static bool bk7258_wifi_runtime_ensure_ready(FAR const char *what)
{
  int pid;
  int i;

  if (bk7258_wifi_is_ready())
    {
      return true;
    }

  pid = kthread_create("bk7258-wifi", BK7258_WIFI_APP_PRIORITY,
                       BK7258_WIFI_APP_STACKSIZE,
                       bk7258_wifi_runtime_worker, NULL);
  if (pid < 0)
    {
      printf("[bk7258_wifi_runtime] %s: init thread failed: %d\n", what, pid);
      return false;
    }

  for (i = 0; i < 300 && !bk7258_wifi_is_ready(); i++)
    {
      usleep(100 * 1000);
    }

  if (!bk7258_wifi_is_ready())
    {
      printf("[bk7258_wifi_runtime] %s: init did not complete; "
             "see syslog above\n", what);
      return false;
    }

  return true;
}

/* Association takes a while: the vendor first runs a directed scan, then
 * authenticates, associates and completes the 4-way handshake.  The reference
 * run on the authoritative board spends ~1.7 s in the scan alone, so poll well
 * past that before giving up. */

#define BK7258_WIFI_CONNECT_POLL_MS     100
#define BK7258_WIFI_CONNECT_POLL_COUNT  300   /* 30 s total */

static int bk7258_wifi_runtime_connect(FAR const char *ssid,
                                       FAR const char *psk)
{
  FAR char *args[3];
  int last_state = -1;
  int state;
  int reason;
  int pid;
  int i;

  /* The credentials reach the worker through argv, which NuttX copies onto
   * the new task's stack, so they stay valid after this function returns and
   * disappear with the worker.  Only the SSID and the passphrase LENGTH are
   * ever printed -- never the passphrase itself. */

  args[0] = (FAR char *)ssid;
  args[1] = (FAR char *)psk;
  args[2] = NULL;

  printf("[bk7258_wifi_runtime] connect: ssid=\"%s\", psk %u chars\n",
         ssid, (unsigned)strlen(psk));

  pid = kthread_create("bk7258-connect", BK7258_WIFI_APP_PRIORITY,
                       BK7258_WIFI_APP_STACKSIZE,
                       bk7258_wifi_connect_worker, args);
  if (pid < 0)
    {
      printf("[bk7258_wifi_runtime] connect: thread failed: %d\n", pid);
      return 1;
    }

  /* Report every state change as it happens, so a failure shows WHERE it
   * stopped rather than just that it never connected.  There is deliberately
   * no early exit on a failure state: the vendor reports DISCONNECTED both
   * before the attempt starts and after it fails, so bailing on it would
   * often abort a connection still in progress.  A genuine failure is read
   * from the reason code in the final line below. */

  for (i = 0; i < BK7258_WIFI_CONNECT_POLL_COUNT; i++)
    {
      if (bk7258_wifi_sta_connect_status(&state, &reason) == 0 &&
          state != last_state)
        {
          printf("  state=%s(%d) reason=%s(%d)\n",
                 bk7258_wifi_sta_state_str(state), state,
                 bk7258_wifi_sta_reason_str(reason), reason);
          last_state = state;
        }

      if (bk7258_wifi_sta_is_connected())
        {
          printf("[bk7258_wifi_runtime] connect: CONNECTED\n");
          return 0;
        }

      usleep(BK7258_WIFI_CONNECT_POLL_MS * 1000);
    }

  state = -1;
  reason = -1;
  bk7258_wifi_sta_connect_status(&state, &reason);
  printf("[bk7258_wifi_runtime] connect: not connected after %d s; "
         "final state=%s(%d) reason=%s(%d)\n",
         (BK7258_WIFI_CONNECT_POLL_MS * BK7258_WIFI_CONNECT_POLL_COUNT) / 1000,
         bk7258_wifi_sta_state_str(state), state,
         bk7258_wifi_sta_reason_str(reason), reason);
  return 1;
}

static void bk7258_wifi_runtime_usage(void)
{
  printf("usage: bk7258_wifi_runtime init\n"
         "       bk7258_wifi_runtime scan [ssid]\n"
         "       bk7258_wifi_runtime connect <ssid> <psk>\n");
}

int main(int argc, char **argv)
{
  if (argc < 2)
    {
      bk7258_wifi_runtime_usage();
      return 1;
    }

  if (strcmp(argv[1], "init") == 0 && argc == 2)
    {
      return bk7258_wifi_runtime_init();
    }

  if (strcmp(argv[1], "scan") == 0 && argc <= 3)
    {
      if (!bk7258_wifi_runtime_ensure_ready("scan"))
        {
          return 1;
        }

      /* `scan` alone stays a broadcast scan; `scan <ssid>` runs the directed
       * form the authority reference run used (scanu_start_req ssid_len=3). */

      return bk7258_wifi_runtime_scan(argc == 3 ? argv[2] : NULL);
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      /* Credentials are command-line only, by design: nothing here writes the
       * passphrase to a file, a defconfig or a compiled-in default. */

      if (argc != 4)
        {
          printf("[bk7258_wifi_runtime] connect needs an SSID and a PSK\n");
          bk7258_wifi_runtime_usage();
          return 1;
        }

      if (!bk7258_wifi_runtime_ensure_ready("connect"))
        {
          return 1;
        }

      return bk7258_wifi_runtime_connect(argv[2], argv[3]);
    }

  bk7258_wifi_runtime_usage();
  return 1;
}
