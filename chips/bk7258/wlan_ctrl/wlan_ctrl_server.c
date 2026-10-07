/****************************************************************************
 * chips/bk7258/wlan_ctrl/wlan_ctrl_server.c
 *
 * CP half of the cross-core Wi-Fi control plane (wlan_ctrl.h).
 *
 * wlan0 and the Armino STA live here, so the work happens here; AP only
 * sends requests and reads results.  The transport is a raw rpmsg
 * endpoint, the same shape as chips/bk7258/mb_ipc/bk_mailbox_rpmsg.c --
 * rpmsg_register_callback + rpmsg_create_ept + our own rx callback.  It is
 * deliberately NOT rpmsg_char: that one creates a /dev/rpmsg-<name> node
 * but on this board the node never appeared (device_created did not
 * register it), so the channel could not be opened.  The mb_ipc form is
 * proven here.
 *
 * The operations below are the single copy of the Armino call order.  The
 * wext face (compat/bk7258_armino_wops.c) calls the same four functions,
 * so nothing is duplicated and the two cannot race into the connect twice:
 *
 *   connect    = demo_sta_app_init(ssid, pass)
 *                (= bk_wifi_sta_set_config() + bk_wifi_sta_start(),
 *                 wifi_api.c:183)
 *   disconnect = bk_wifi_sta_disconnect() then bk_wifi_sta_stop()
 *                (wifi.h:97/:103: stop alone does not fire
 *                 EVENT_WIFI_STA_DISCONNECTED, and a restart needs the
 *                 STARTED bit cleared first -- wifi_v2.c:2429)
 *
 * Connect and disconnect run on a thread of their own: Armino's
 * association is deep and slow, and a long call in the rx callback would
 * stall every later message.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kthread.h>
#include <nuttx/rpmsg/rpmsg.h>

#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include <components/event.h>

#include "wlan_ctrl.h"

/* bk_private/bk_wifi.h:503 -- two-argument form in this configuration. */

int demo_sta_app_init(char *oob_ssid, char *connect_key);

#define WLAN_PEER_CPUNAME   "ap"

static struct rpmsg_endpoint s_ept;
static bool s_ept_bound;

/* Scan results are fetched once and served by index; see the SCAN_GET case.
 */

static struct wifi_ap_info_s s_aps[32];
static int s_n;
static bool s_cached;

/****************************************************************************
 * The operations
 ****************************************************************************/

int bk7258_wlan_scan_start(void)
{
  wifi_scan_config_t cfg;

  memset(&cfg, 0, sizeof(cfg));
  cfg.scan_type = 0;   /* active */
  s_cached = false;
  s_n = 0;
  return bk_wifi_scan_start(&cfg) < 0 ? -EIO : 0;
}

/* wifi.h:226 -- results are handed over from the scan-done event, not by
 * polling bk_wifi_scan_get_result(): called before EVENT_WIFI_SCAN_DONE it
 * finds nothing and the list is gone by the time the scan really finishes.
 */

static bk_err_t scan_done_handler(void *arg, event_module_t event_module,
                                  int event_id, void *event_data)
{
  wifi_scan_result_t res;
  int i;

  (void)arg;
  (void)event_module;
  (void)event_id;
  (void)event_data;

  memset(&res, 0, sizeof(res));
  if (bk_wifi_scan_get_result(&res) < 0)
    {
      return BK_FAIL;
    }

  for (i = 0; i < res.ap_num && i < 32 && res.aps != NULL; i++)
    {
      const wifi_scan_ap_info_t *a = &res.aps[i];
      struct wifi_ap_info_s *o = &s_aps[i];

      memset(o, 0, sizeof(*o));
      strlcpy(o->ssid, a->ssid, sizeof(o->ssid));
      memcpy(o->bssid, a->bssid, 6);
      o->rssi = (int8_t)a->rssi;
      o->channel = a->channel;
      o->secured = (a->security != WIFI_SECURITY_NONE);
    }

  bk_wifi_scan_free_result(&res);
  s_n = i;
  s_cached = true;
  syslog(LOG_INFO, "[WLAN-CTRL] scan done: %d APs\n", s_n);
  return BK_OK;
}

static void scan_watch_arm(void)
{
  static bool armed;
  int ret;

  if (!armed)
    {
      ret = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                 scan_done_handler, NULL);
      syslog(LOG_INFO, "[WLAN-CTRL] register scan-done cb -> %d\n", ret);
      armed = true;
    }
}

/* Connect and disconnect are deep and slow; hand them to a thread so the
 * request path stays responsive.  One at a time: a second connect while
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
 * The rpmsg endpoint
 ****************************************************************************/

static int wlan_send(FAR const struct wlan_ctrl_msg_s *msg)
{
  int ret;

  if (!s_ept_bound)
    {
      return -ENOTCONN;
    }

  /* Non-blocking, like mb_ipc: this runs inside the rpmsg receive path,
   * where waiting for a TX buffer can deadlock against the very code that
   * releases one.
   */

  ret = rpmsg_send_offchannel_raw(&s_ept, s_ept.addr, s_ept.dest_addr,
                                  msg, sizeof(*msg), false);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[WLAN-CTRL] reply op=%u -> %d (addr=%08x dst=%08x)\n",
             msg->op, ret, s_ept.addr, s_ept.dest_addr);
    }

  return ret < 0 ? ret : 0;
}

static int wlan_rx(FAR struct rpmsg_endpoint *ept, FAR void *data,
                   size_t len, uint32_t src, FAR void *priv)
{
  FAR const struct wlan_ctrl_msg_s *req = data;
  struct wifi_ap_info_s aps[32];
  struct wlan_ctrl_msg_s rep;
  int n = 0;
  int ret;
  int i;

  (void)ept;
  (void)src;
  (void)priv;

  if (len < sizeof(*req))
    {
      return 0;
    }

  switch (req->op)
    {
      case WLAN_CTRL_OP_SCAN_START:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_SCAN_START;
        scan_watch_arm();
        rep.result = bk7258_wlan_scan_start();
        wlan_send(&rep);
        break;

      case WLAN_CTRL_OP_SCAN_GET:
        /* One AP per request and one reply per request: a burst of 20
         * replies would exhaust the four TX buffers this link has.  The
         * list is filled by scan_done_handler() and served by index; until
         * that event has fired the caller just polls again.
         */

        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_SCAN_GET;
        if (!s_cached)
          {
            rep.result = -EAGAIN;
            wlan_send(&rep);
            break;
          }

        rep.result = 0;
        rep.total = (uint16_t)s_n;
        rep.index = req->index;
        if (req->index < s_n)
          {
            rep.ap = s_aps[req->index];
          }
        else
          {
            rep.result = -ENOENT;
          }

        wlan_send(&rep);
        break;

      case WLAN_CTRL_OP_CONNECT:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_CONNECT;
        rep.result = bk7258_wlan_connect(req->ssid, req->pass);
        wlan_send(&rep);
        break;

      case WLAN_CTRL_OP_DISCONNECT:
        memset(&rep, 0, sizeof(rep));
        rep.op = WLAN_CTRL_OP_DISCONNECT;
        rep.result = bk7258_wlan_disconnect();
        wlan_send(&rep);
        break;

      default:
        memset(&rep, 0, sizeof(rep));
        rep.op = req->op;
        rep.result = -ENOTTY;
        wlan_send(&rep);
        break;
    }

  return 0;
}

static void wlan_created(FAR struct rpmsg_device *rdev, FAR void *priv)
{
  int ret;

  if (strcmp(rpmsg_get_cpuname(rdev), WLAN_PEER_CPUNAME) != 0 ||
      s_ept_bound)
    {
      return;
    }

  s_ept.priv = &s_ept;
  ret = rpmsg_create_ept(&s_ept, rdev, WLAN_CTRL_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, wlan_rx, NULL);
  if (ret < 0)
    {
      s_ept.priv = NULL;
      syslog(LOG_ERR, "[WLAN-CTRL] create ept: %d\n", ret);
      return;
    }

  s_ept_bound = true;
  syslog(LOG_INFO, "[WLAN-CTRL] CP endpoint bound (%s)\n", WLAN_CTRL_NAME);
}

static void wlan_destroyed(FAR struct rpmsg_device *rdev, FAR void *priv)
{
  if (s_ept_bound && strcmp(rpmsg_get_cpuname(rdev), WLAN_PEER_CPUNAME) == 0)
    {
      rpmsg_destroy_ept(&s_ept);
      s_ept.priv = NULL;
      s_ept_bound = false;
    }
}

int wlan_ctrl_server_init(void)
{
  int ret;

  ret = rpmsg_register_callback(&s_ept, wlan_created, wlan_destroyed,
                                NULL, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[WLAN-CTRL] register callback: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "[WLAN-CTRL] CP half registered\n");
  return 0;
}
