/****************************************************************************
 * app/bk7258_platform_test/netspd.c
 *
 * RPMsg frame-path throughput probe for the CP/AP pair.
 *
 * The point is to answer one question before anyone invests in a virtual NIC
 * (rpmsgdrv/VNET): how fast can the two cores move ordinary frame-sized
 * messages?  usrsock is limited by one synchronous round trip per send(), which
 * measured 3-4 ms regardless of payload size, so an AP application tops out at
 * ~4.3 Mbit/s.  A frame-level path has no per-call round trip, so if the
 * transport itself is fast, VNET is the way to go; if it is not, neither is.
 *
 * Both endpoints are created with an EMPTY name and explicit src/dest
 * addresses.  openamp only announces an endpoint over NS when ept->name[] is
 * non-empty (open-amp/lib/rpmsg/rpmsg.c:400), so this binds without any
 * announcement and therefore works in either start order -- which matters
 * because the board has one console and the two roles cannot be started
 * simultaneously by hand.  Addresses are above RPMSG_RESERVED_ADDRESSES so the
 * dynamic allocator never hands them out.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/rpmsg/rpmsg.h>

#ifdef CONFIG_BK7258_COMPONENT_CP
#  define NETSPD_ADDR_LOCAL 0x220u
#  define NETSPD_ADDR_PEER  0x221u
#else
#  define NETSPD_ADDR_LOCAL 0x221u
#  define NETSPD_ADDR_PEER  0x220u
#endif

#define NETSPD_BUFMAX 2048

static struct rpmsg_endpoint g_ept;
static sem_t g_echo_sem;
static volatile uint32_t g_echo_len;
static volatile uint32_t g_echo_count;
static volatile uint32_t g_echo_bytes;
static bool g_bound;
static int g_echo_seconds;
static bool g_count_only;
static uint8_t g_rx_buf[NETSPD_BUFMAX];
static volatile uint32_t g_rx_len;
static sem_t g_rx_sem;

static int netspd_ept_cb(FAR struct rpmsg_endpoint *ept, FAR void *data,
                         size_t len, uint32_t src, FAR void *priv)
{
  if (len > sizeof(uint32_t) && ((FAR uint32_t *)data)[0] == 0xfeedfaceu)
    {
      /* Stop marker from the sender. */

      sem_post(&g_echo_sem);
      return 0;
    }

  g_echo_count++;
  g_echo_bytes += len;
  g_echo_len = len;

  /* Hand the frame to the echo thread instead of sending from here: this
   * callback runs in the rpmsg work queue, and rpmsg_send() blocks until a TX
   * buffer is free.  With the ring full that waits on the very thread that
   * would drain it -- the AP stalled and stopped answering the heartbeat.
   */

  if (!g_count_only && len <= sizeof(g_rx_buf))
    {
      memcpy(g_rx_buf, data, len);
      g_rx_len = len;
      sem_post(&g_rx_sem);
    }

  sem_post(&g_echo_sem);
  return 0;
}

static void netspd_created(FAR struct rpmsg_device *rdev, FAR void *priv)
{
  int ret;

  if (g_bound)
    {
      return;
    }

  ret = rpmsg_create_ept(&g_ept, rdev, "", NETSPD_ADDR_LOCAL,
                         NETSPD_ADDR_PEER, netspd_ept_cb, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[netspd] create ept: %d\n", ret);
      return;
    }

  g_bound = true;
  syslog(LOG_INFO, "[netspd] bound local=%#x peer=%#x\n",
         NETSPD_ADDR_LOCAL, NETSPD_ADDR_PEER);
}

static void netspd_destroyed(FAR struct rpmsg_device *rdev, FAR void *priv)
{
  if (g_bound)
    {
      rpmsg_destroy_ept(&g_ept);
      g_bound = false;
    }
}

static int netspd_wait_bound(int seconds)
{
  int i;

  for (i = 0; i < seconds * 10 && !g_bound; i++)
    {
      usleep(100000);
    }

  return g_bound ? 0 : -ETIMEDOUT;
}

/* The echo side runs as its own kernel thread: the board has a single
 * console, so the AP's shell has to stay usable while echo waits for traffic --
 * otherwise there is no prompt left to type cu's "~." escape at.
 */

static int netspd_echo_thread(int argc, FAR char *argv[])
{
  clock_t start;

  rpmsg_register_callback(NULL, netspd_created, netspd_destroyed, NULL, NULL);
  netspd_wait_bound(5);

  printf("[netspd] echo: waiting up to %d s for traffic\n", g_echo_seconds);
  start = clock_systime_ticks();

  while (TICK2MSEC(clock_systime_ticks() - start) <
         (uint32_t)g_echo_seconds * 1000)
    {
      struct timespec ts;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 100 * 1000 * 1000;
      if (ts.tv_nsec >= 1000000000)
        {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000;
        }

      if (sem_timedwait(&g_rx_sem, &ts) == 0 && g_rx_len > 0)
        {
          rpmsg_send(&g_ept, g_rx_buf, g_rx_len);
        }
    }

  printf("[netspd] echo: rx=%lu msgs, %lu bytes\n",
         (unsigned long)g_echo_count, (unsigned long)g_echo_bytes);
  return 0;
}

static int netspd_echo(int seconds)
{
  g_echo_seconds = seconds;

  if (kthread_create("netspd-echo", 100, 4096, netspd_echo_thread, NULL) < 0)
    {
      printf("netspd: cannot start echo thread\n");
      return -ENOMEM;
    }

  printf("[netspd] echo: started in the background for %d s\n", seconds);
  return 0;
}

static int netspd_send(int times, int len)
{
  static uint8_t buf[NETSPD_BUFMAX];
  clock_t start;
  clock_t elapsed;
  uint32_t ok = 0;
  int i;
  int ret;

  if (len < 8 || len > NETSPD_BUFMAX)
    {
      printf("netspd: len must be 8..%d\n", NETSPD_BUFMAX);
      return -EINVAL;
    }

  rpmsg_register_callback(NULL, netspd_created, netspd_destroyed, NULL, NULL);
  netspd_wait_bound(5);
  if (!g_bound)
    {
      printf("netspd: endpoint not bound\n");
      return -ETIMEDOUT;
    }

  memset(buf, 0x5a, sizeof(buf));
  ((FAR uint32_t *)buf)[0] = 0x600df00du;

  start = clock_systime_ticks();

  for (i = 0; i < times; i++)
    {
      ret = rpmsg_send(&g_ept, buf, len);
      if (ret < 0)
        {
          printf("netspd: send %d failed: %d\n", i, ret);
          break;
        }

      /* sem_wait, not sem_timedwait: NuttX's timed variant rejects a NULL
       * abstime, which made the loop exit on the first message with 0 echoes.
       */

      while (sem_wait(&g_echo_sem) < 0)
        {
          if (errno != EINTR)
            {
              printf("netspd: wait failed: %d\n", errno);
              return -errno;
            }
        }

      if (g_echo_len == (uint32_t)len)
        {
          ok++;
        }
    }

  elapsed = clock_systime_ticks() - start;

  /* Stop marker so the echo side can report its own counters. */

  ((FAR uint32_t *)buf)[0] = 0xfeedfaceu;
  rpmsg_send(&g_ept, buf, 8);

  printf("[netspd] send: %lu/%d echoes x %d B in %lu ms -> %lu kbit/s, "
         "avg rtt %lu us\n",
         (unsigned long)ok, times, len, (unsigned long)TICK2MSEC(elapsed),
         (unsigned long)(elapsed ? (TICK2MSEC(elapsed) * 1000u /
                                    (uint32_t)ok) : 0),
         (unsigned long)(ok ? TICK2MSEC(elapsed) * 1000u / ok : 0));
  return 0;
}

/* One-way blast: no per-message echo and no wait.  This is the shape a
 * virtual NIC uses -- frames are queued and the sender only blocks when the
 * ring itself is full -- so its rate is what a VNET port would inherit.
 * The peer should be running "netspd count <seconds>".
 */

static int netspd_blast(int times, int len)
{
  static uint8_t buf[NETSPD_BUFMAX];
  clock_t start;
  clock_t elapsed;
  uint32_t sent = 0;
  int i;
  int ret;

  if (len < 8 || len > NETSPD_BUFMAX)
    {
      printf("netspd: len must be 8..%d\n", NETSPD_BUFMAX);
      return -EINVAL;
    }

  rpmsg_register_callback(NULL, netspd_created, netspd_destroyed, NULL, NULL);
  netspd_wait_bound(5);
  if (!g_bound)
    {
      printf("netspd: endpoint not bound\n");
      return -ETIMEDOUT;
    }

  memset(buf, 0x5a, sizeof(buf));
  ((FAR uint32_t *)buf)[0] = 0x600df00du;

  start = clock_systime_ticks();
  for (i = 0; i < times; i++)
    {
      ret = rpmsg_send(&g_ept, buf, len);
      if (ret < 0)
        {
          printf("netspd: blast %d failed: %d\n", i, ret);
          break;
        }

      sent++;
    }

  elapsed = clock_systime_ticks() - start;
  /* bits per millisecond IS kbit/s; the earlier print divided by 1000 once
   * too often and reported Mbit/s as kbit/s.
   */

  printf("[netspd] blast: %lu frames x %d B in %lu ms -> %lu kbit/s "
         "(%lu frames/s)\n",
         (unsigned long)sent, len, (unsigned long)TICK2MSEC(elapsed),
         (unsigned long)(elapsed ? ((uint64_t)sent * len * 8) /
                                   TICK2MSEC(elapsed) : 0),
         (unsigned long)(elapsed ? (uint64_t)sent * 1000 /
                                   TICK2MSEC(elapsed) : 0));
  return 0;
}

int main(int argc, FAR char *argv[])
{
  sem_init(&g_echo_sem, 0, 0);
  sem_init(&g_rx_sem, 0, 0);

  if (argc >= 3 && strcmp(argv[1], "echo") == 0)
    {
      return netspd_echo(atoi(argv[2]));
    }

  if (argc >= 3 && strcmp(argv[1], "count") == 0)
    {
      g_count_only = true;
      return netspd_echo(atoi(argv[2]));
    }

  if (argc >= 4 && strcmp(argv[1], "send") == 0)
    {
      return netspd_send(atoi(argv[2]), atoi(argv[3]));
    }

  if (argc >= 4 && strcmp(argv[1], "blast") == 0)
    {
      return netspd_blast(atoi(argv[2]), atoi(argv[3]));
    }

  printf("Usage: netspd echo <seconds>\n");
  printf("       netspd count <seconds>\n");
  printf("       netspd send <times> <len>\n");
  printf("       netspd blast <times> <len>\n");
  return -EINVAL;
}
