/****************************************************************************
 * chips/bk7258/wlan_ctrl/wlan_ctrl_client.c
 *
 * AP half of the cross-core Wi-Fi control plane (wlan_ctrl.h).
 *
 * Same transport as the CP side and as mb_ipc: a raw rpmsg endpoint with
 * our own rx callback.  Applications on AP -- the Wi-Fi UI, ai_agent,
 * later MCP -- only ever see wifi_ctrl_*().
 *
 * One AP per message on the wire, so a scan of any size is a sequence of
 * small messages and nothing has to be reassembled.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mqueue.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "wlan_ctrl.h"

/* Replies arrive in the RPMsg receive path, which runs in a different task
 * group than the wlan_ctrl_* caller — the public mq_open() hands back a file
 * descriptor and mq_send() resolves it through the *current* task's fd table,
 * so the rx callback sees -EBADF.  The file_mq_* family takes a struct file
 * directly and is shared across task groups; that is the API for this handoff.
 */

static struct file s_reply_file;
static bool s_reply_open;

#define WLAN_PEER_CPUNAME   "cp"
#define WLAN_REPLY_QUEUE    40

static struct rpmsg_endpoint s_ept;
static bool s_ept_bound;

/* Replies arrive in the rx callback; hand them to the caller through a
 * queue so the callback never blocks and a scan can stream its APs.
 */

static volatile int s_rx_count;

/****************************************************************************
 * The channel
 ****************************************************************************/

static int wlan_rx(FAR struct rpmsg_endpoint *ept, FAR void *data,
                   size_t len, uint32_t src, FAR void *priv)
{
  int ret;

  (void)ept;
  (void)src;
  (void)priv;

  if (len < sizeof(struct wlan_ctrl_msg_s) || !s_reply_open)
    {
      return 0;
    }

  s_rx_count++;
  ret = file_mq_ticksend(&s_reply_file, data, sizeof(struct wlan_ctrl_msg_s),
                         0, 0);
  if (ret < 0)
    {
      s_rx_count = ret;
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
  syslog(LOG_INFO, "[WLAN-CTRL] AP endpoint bound (%s)\n", WLAN_CTRL_NAME);
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

static int wlan_ctrl_open(void)
{
  struct mq_attr attr;
  static bool registered;
  int ret;

  if (s_ept_bound)
    {
      return 0;
    }

  if (!s_reply_open)
    {
      attr.mq_maxmsg  = WLAN_REPLY_QUEUE;
      attr.mq_msgsize = sizeof(struct wlan_ctrl_msg_s);
      ret = file_mq_open(&s_reply_file, "wlan-ctrl-reply",
                         O_RDWR | O_CREAT, 0666, &attr);
      printf("[wlan-ctrl] file_mq_open -> %d inode=%p oflags=%x\n",
             ret, s_reply_file.f_inode, s_reply_file.f_oflags);
      if (ret < 0)
        {
          return ret;
        }

      s_reply_open = true;
    }

  if (!registered)
    {
      int ret = rpmsg_register_callback(&s_ept, wlan_created,
                                        wlan_destroyed, NULL, NULL);
      if (ret < 0)
        {
          return ret;
        }

      registered = true;
    }

  /* device_created fires from rpmsg_register_callback for every rpmsg
   * device already up, so the endpoint is normally bound by now.  Give it
   * a moment anyway rather than reporting a spurious ENOTCONN.
   */

  /* Bound is not the same as created: with RPMSG_ADDR_ANY the endpoint is
   * usable only once the peer's end has been paired with it, which shows
   * up as dest_addr moving off RPMSG_ADDR_ANY (0xffffffff).
   */

  for (int i = 0; i < 50; i++)
    {
      if (s_ept_bound && s_ept.dest_addr != RPMSG_ADDR_ANY)
        {
          return 0;
        }

      usleep(100 * 1000);
    }

  return s_ept_bound ? -ENOTCONN : -ENOTCONN;
}

static int wlan_ctrl_send(FAR const struct wlan_ctrl_msg_s *msg)
{
  int ret;

  ret = wlan_ctrl_open();
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsg_send_offchannel_raw(&s_ept, s_ept.addr, s_ept.dest_addr,
                                  msg, sizeof(*msg), true);
  return ret < 0 ? ret : 0;
}

static int wlan_ctrl_recv(FAR struct wlan_ctrl_msg_s *rep)
{
  int ret;

  ret = file_mq_tickreceive(&s_reply_file, (FAR char *)rep, sizeof(*rep),
                            NULL, MSEC2TICK(5000));
  if (ret < 0)
    {
      printf("[wlan-ctrl] recv -> %d (rx=%d bound=%d dst=%08x)\n",
             ret, s_rx_count, s_ept_bound, s_ept.dest_addr);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * The API
 ****************************************************************************/

static int wlan_ctrl_call(FAR struct wlan_ctrl_msg_s *msg)
{
  struct wlan_ctrl_msg_s rep;
  int ret;

  ret = wlan_ctrl_send(msg);
  if (ret < 0)
    {
      return ret;
    }

  ret = wlan_ctrl_recv(&rep);
  if (ret < 0)
    {
      return ret;
    }

  return rep.result < 0 ? rep.result : 0;
}

int wifi_ctrl_scan_start(void)
{
  struct wlan_ctrl_msg_s msg;

  memset(&msg, 0, sizeof(msg));
  msg.op = WLAN_CTRL_OP_SCAN_START;
  return wlan_ctrl_call(&msg);
}

int wifi_ctrl_scan_get(FAR struct wifi_ap_info_s *aps, int max, int *out_n)
{
  struct wlan_ctrl_msg_s msg;
  struct wlan_ctrl_msg_s rep;
  int n = 0;
  int ret;

  if (aps == NULL || out_n == NULL || max <= 0)
    {
      return -EINVAL;
    }

  /* One AP per request and one reply per request.  The server keeps the
   * list and hands out pages by index, so the link never has to carry
   * more than one message at a time.
   */

  for (; ; )
    {
      memset(&msg, 0, sizeof(msg));
      msg.op = WLAN_CTRL_OP_SCAN_GET;
      msg.index = (uint16_t)n;

      ret = wlan_ctrl_send(&msg);
      if (ret < 0)
        {
          return ret;
        }

      ret = wlan_ctrl_recv(&rep);
      if (ret < 0)
        {
          return ret;
        }

      if (rep.op != WLAN_CTRL_OP_SCAN_GET)
        {
          return -EPROTO;
        }

      if (rep.result == -EAGAIN)
        {
          /* Scan not finished on CP yet; caller polls again. */

          *out_n = 0;
          return 0;
        }

      if (rep.result < 0)
        {
          break;
        }

      if (n < max)
        {
          aps[n++] = rep.ap;
        }

      if (n >= rep.total)
        {
          break;
        }
    }

  *out_n = n;
  return 0;
}

int wifi_ctrl_connect(FAR const char *ssid, FAR const char *pass)
{
  struct wlan_ctrl_msg_s msg;
  int ret;

  if (ssid == NULL || pass == NULL)
    {
      return -EINVAL;
    }

  memset(&msg, 0, sizeof(msg));
  msg.op = WLAN_CTRL_OP_CONNECT;
  strlcpy(msg.ssid, ssid, sizeof(msg.ssid));
  strlcpy(msg.pass, pass, sizeof(msg.pass));

  return wlan_ctrl_call(&msg);
}

int wifi_ctrl_disconnect(void)
{
  struct wlan_ctrl_msg_s msg;

  memset(&msg, 0, sizeof(msg));
  msg.op = WLAN_CTRL_OP_DISCONNECT;
  return wlan_ctrl_call(&msg);
}

int wifi_ctrl_status(FAR struct wifi_ap_info_s *ap, bool *connected)
{
  (void)ap;
  (void)connected;
  return -ENOSYS;
}
