/****************************************************************************
 * app/wifi_ctrl/wifi_ctrl.c
 *
 * AP half of the cross-core Wi-Fi control plane (see
 * chips/bk7258/wlan_ctrl/wlan_ctrl.h for the contract and the reason).
 *
 * Everything this file does is userspace: open the rpmsg control node for
 * the peer, create the "wlan-ctrl" endpoint, and talk to the local
 * /dev/rpmsg-wlan-ctrl.  CP does the work and answers; the AP-side callers
 * (the Wi-Fi UI, ai_agent, later MCP) only ever see wifi_ctrl_*().
 *
 * One AP per message on the wire, so a scan of any size is a sequence of
 * small reads rather than one large one -- which is the whole point of not
 * sending this over usrsock.
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/rpmsg/rpmsg.h>

#include "wlan_ctrl.h"

/* The control node names the peer, not ourselves (rpmsg_virtio.c:758
 * builds /dev/rpmsg/<cpuname> from rpmsg->cpuname, the remote CPU).
 * AP talks to CP.
 */

#define WLAN_CTRL_PEER        "cp"
#define WLAN_CTRL_PEER_PATH   "/dev/rpmsg/" WLAN_CTRL_PEER

static int g_fd = -1;

static int wlan_ctrl_open(void)
{
  struct rpmsg_endpoint_info info;
  int cfd;
  int ret;

  if (g_fd >= 0)
    {
      return 0;
    }

  memset(&info, 0, sizeof(info));
  strlcpy(info.name, WLAN_CTRL_NAME, sizeof(info.name));
  info.src = RPMSG_ADDR_ANY;
  info.dst = RPMSG_ADDR_ANY;

  cfd = open(WLAN_CTRL_PEER_PATH, O_RDWR);
  if (cfd < 0)
    {
      return -errno;
    }

  ret = ioctl(cfd, RPMSG_CREATE_DEV_IOCTL, (unsigned long)&info);
  close(cfd);
  if (ret < 0 && ret != -EEXIST)
    {
      return ret;
    }

  g_fd = open(WLAN_CTRL_PATH, O_RDWR);
  return g_fd < 0 ? -errno : 0;
}

/* One request, one reply.  SCAN_GET is the exception: it replies with one
 * message per AP, so it is handled separately below.
 */

static int wlan_ctrl_call(FAR struct wlan_ctrl_msg_s *msg)
{
  struct wlan_ctrl_msg_s rep;
  int ret;

  ret = wlan_ctrl_open();
  if (ret < 0)
    {
      return ret;
    }

  if (write(g_fd, msg, sizeof(*msg)) != sizeof(*msg))
    {
      return -EIO;
    }

  if (read(g_fd, &rep, sizeof(rep)) != sizeof(rep))
    {
      return -EIO;
    }

  if (rep.op != msg->op)
    {
      return -EPROTO;
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

  memset(&msg, 0, sizeof(msg));
  msg.op = WLAN_CTRL_OP_SCAN_GET;

  ret = wlan_ctrl_open();
  if (ret < 0)
    {
      return ret;
    }

  if (write(g_fd, &msg, sizeof(msg)) != sizeof(msg))
    {
      return -EIO;
    }

  /* The server answers with one AP per message and clears `more` on the
   * last one.  Read until then, or until the caller's array is full.
   */

  for (; ; )
    {
      if (read(g_fd, &rep, sizeof(rep)) != sizeof(rep))
        {
          return -EIO;
        }

      if (rep.op != WLAN_CTRL_OP_SCAN_GET)
        {
          return -EPROTO;
        }

      if (rep.result < 0)
        {
          return rep.result;
        }

      if (n < max)
        {
          aps[n++] = rep.ap;
        }

      if (!rep.more)
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
