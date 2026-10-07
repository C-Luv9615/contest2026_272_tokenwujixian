/****************************************************************************
 * chips/bk7258/wlan_ctrl/wlan_ctrl_server.c
 *
 * CP half of the cross-core Wi-Fi control plane (wlan_ctrl.h).
 *
 * wlan0 and the Armino STA live here, so the work happens here; AP only
 * sends requests and reads results.  The channel is rpmsg_char: create the
 * "wlan-ctrl" endpoint and /dev/rpmsg-wlan-ctrl appears on both cores,
 * with read/write meaning one message at a time.
 *
 * The operations below are the single copy of the Armino call order.  The
 * wext face (compat/bk7258_armino_wops.c) calls the same four functions,
 * so nothing is duplicated:
 *
 *   connect    = demo_sta_app_init(ssid, pass)
 *                (= bk_wifi_sta_set_config() + bk_wifi_sta_start(),
 *                 wifi_api.c:183)
 *   disconnect = bk_wifi_sta_disconnect() then bk_wifi_sta_stop()
 *                (wifi.h:97/:103: stop alone does not fire
 *                 EVENT_WIFI_STA_DISCONNECTED, and a restart needs the
 *                 STARTED bit cleared first -- wifi_v2.c:2429)
 *
 * Connect and disconnect run on a thread of their own rather than in the
 * request loop: Armino's association is deep and slow, and a long call
 * here would block every later request.  The request is answered
 * immediately with 0 and the link comes up in the background, with the
 * address arriving through EVENT_WIFI_STA_CONNECTED -> sta_ip_start() ->
 * DHCP as usual.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/kthread.h>
#include <nuttx/rpmsg/rpmsg.h>

#include <modules/wifi.h>
#include <modules/wifi_types.h>

#include "wlan_ctrl.h"

/* bk_private/bk_wifi.h:503 -- two-argument form in this configuration. */

int demo_sta_app_init(char *oob_ssid, char *connect_key);

/****************************************************************************
 * The operations
 ****************************************************************************/

int bk7258_wlan_scan_start(void)
{
  wifi_scan_config_t cfg;

  memset(&cfg, 0, sizeof(cfg));
  cfg.scan_type = 0;   /* active */
  return bk_wifi_scan_start(&cfg) < 0 ? -EIO : 0;
}

int bk7258_wlan_scan_get(FAR struct wifi_ap_info_s *aps, int max,
                         int *out_n)
{
  wifi_scan_result_t res;
  int i;

  memset(&res, 0, sizeof(res));
  if (bk_wifi_scan_get_result(&res) < 0)
    {
      return -EAGAIN;
    }

  for (i = 0; i < res.ap_num && i < max && res.aps != NULL; i++)
    {
      const wifi_scan_ap_info_t *a = &res.aps[i];
      struct wifi_ap_info_s *o = &aps[i];

      memset(o, 0, sizeof(*o));
      strlcpy(o->ssid, a->ssid, sizeof(o->ssid));
      memcpy(o->bssid, a->bssid, 6);
      o->rssi = (int8_t)a->rssi;
      o->channel = a->channel;
      o->secured = (a->security != WIFI_SECURITY_NONE);
    }

  bk_wifi_scan_free_result(&res);
  *out_n = i;
  return 0;
}

/* The connect/disconnect work is deep and slow; hand it to a thread so the
 * request loop stays responsive.  One at a time: a second connect while
 * one is running would only be wifi_v2.c:2429 "sta already started".
 */

#define WLAN_WORKER_STACKSIZE   8192
#define WLAN_WORKER_PRIORITY    200

enum wlan_job_e
{
  WLAN_JOB_IDLE,
  WLAN_JOB_CONNECT,
  WLAN_JOB_DISCONNECT,
};

static struct
{
  enum wlan_job_e what;
  char ssid[WLAN_CTRL_SSID_LEN];
  char pass[WLAN_CTRL_PASS_LEN];
} s_job;

static int wlan_worker(int argc, FAR char *argv[])
{
  (void)argc;
  (void)argv;

  switch (s_job.what)
    {
      case WLAN_JOB_CONNECT:
        demo_sta_app_init(s_job.ssid, s_job.pass);
        break;

      case WLAN_JOB_DISCONNECT:
        bk_wifi_sta_disconnect();
        bk_wifi_sta_stop();
        break;

      default:
        break;
    }

  s_job.what = WLAN_JOB_IDLE;
  return 0;
}

static int wlan_dispatch(enum wlan_job_e what)
{
  pid_t pid;

  if (s_job.what != WLAN_JOB_IDLE)
    {
      return -EBUSY;
    }

  s_job.what = what;
  pid = kthread_create("wlan-ctrl", WLAN_WORKER_PRIORITY,
                       WLAN_WORKER_STACKSIZE, wlan_worker, NULL);
  if (pid < 0)
    {
      s_job.what = WLAN_JOB_IDLE;
      return pid;
    }

  return 0;
}

int bk7258_wlan_connect(FAR const char *ssid, FAR const char *pass)
{
  if (ssid == NULL || pass == NULL || ssid[0] == '\0')
    {
      return -EINVAL;
    }

  if (strlen(ssid) > WLAN_CTRL_SSID_LEN - 1 ||
      strlen(pass) > WLAN_CTRL_PASS_LEN - 1)
    {
      return -E2BIG;
    }

  strlcpy(s_job.ssid, ssid, sizeof(s_job.ssid));
  strlcpy(s_job.pass, pass, sizeof(s_job.pass));
  return wlan_dispatch(WLAN_JOB_CONNECT);
}

int bk7258_wlan_disconnect(void)
{
  return wlan_dispatch(WLAN_JOB_DISCONNECT);
}

/****************************************************************************
 * The request loop
 ****************************************************************************/

static void wlan_handle(int fd, FAR const struct wlan_ctrl_msg_s *req)
{
  struct wifi_ap_info_s aps[32];
  struct wlan_ctrl_msg_s rep;
  int n = 0;
  int ret;
  int i;

  switch (req->op)
    {
      case WLAN_CTRL_OP_SCAN_START:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_SCAN_START;
        rep.result = bk7258_wlan_scan_start();
        write(fd, &rep, sizeof(rep));
        break;

      case WLAN_CTRL_OP_SCAN_GET:
        ret = bk7258_wlan_scan_get(aps, 32, &n);
        if (ret < 0)
          {
            memset(&rep, 0, sizeof(rep));
            rep.op = WLAN_CTRL_OP_SCAN_GET;
            rep.result = ret;
            write(fd, &rep, sizeof(rep));
            break;
          }

        /* One AP per message so nothing has to be reassembled; `more` is
         * cleared on the last one so the reader knows where to stop.
         */

        for (i = 0; i < n; i++)
          {
            memset(&rep, 0, sizeof(rep));
            rep.op = WLAN_CTRL_OP_SCAN_GET;
            rep.result = 0;
            rep.more = (i + 1 < n);
            rep.total = (uint16_t)n;
            rep.ap = aps[i];
            write(fd, &rep, sizeof(rep));
          }

        if (n == 0)
          {
            memset(&rep, 0, sizeof(rep));
            rep.op = WLAN_CTRL_OP_SCAN_GET;
            rep.result = 0;
            rep.more = 0;
            write(fd, &rep, sizeof(rep));
          }

        break;

      case WLAN_CTRL_OP_CONNECT:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_CONNECT;
        rep.result = bk7258_wlan_connect(req->ssid, req->pass);
        write(fd, &rep, sizeof(rep));
        break;

      case WLAN_CTRL_OP_DISCONNECT:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_DISCONNECT;
        rep.result = bk7258_wlan_disconnect();
        write(fd, &rep, sizeof(rep));
        break;

      case WLAN_CTRL_OP_STATUS:
      default:
        memset(&rep, 0, sizeof(rep));
        rep.op = req->op;
        rep.result = -ENOTTY;
        write(fd, &rep, sizeof(rep));
        break;
    }
}

static int wlan_server_loop(int argc, FAR char *argv[])
{
  struct wlan_ctrl_msg_s req;
  int fd;

  (void)argc;
  (void)argv;

  fd = open(WLAN_CTRL_PATH, O_RDWR);
  if (fd < 0)
    {
      syslog(LOG_ERR, "[WLAN-CTRL] open %s: %d\n", WLAN_CTRL_PATH, errno);
      return 1;
    }

  syslog(LOG_INFO, "[WLAN-CTRL] CP request loop on %s\n", WLAN_CTRL_PATH);

  for (; ; )
    {
      ssize_t n = read(fd, &req, sizeof(req));

      if (n < (ssize_t)sizeof(req))
        {
          if (n < 0 && errno == EINTR)
            {
              continue;
            }

          break;
        }

      wlan_handle(fd, &req);
    }

  close(fd);
  return 0;
}

int wlan_ctrl_server_init(void)
{
  struct rpmsg_endpoint_info info;
  int ret;

  memset(&info, 0, sizeof(info));
  strlcpy(info.name, WLAN_CTRL_NAME, sizeof(info.name));
  info.src = RPMSG_ADDR_ANY;
  info.dst = RPMSG_ADDR_ANY;

  ret = rpmsg_ioctl("ap", RPMSG_CREATE_DEV_IOCTL, (unsigned long)&info);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[WLAN-CTRL] create eptdev: %d\n", ret);
      return ret;
    }

  ret = kthread_create("wlan-ctrl", 200, 4096, wlan_server_loop, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[WLAN-CTRL] server thread: %d\n", ret);
      return ret;
    }

  return 0;
}
