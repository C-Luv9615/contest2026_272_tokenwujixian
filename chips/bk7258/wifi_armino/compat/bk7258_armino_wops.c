/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_wops.c
 *
 * The control plane of wlan0: the `wireless_ops_s` half of
 * bk7258_armino_netdev.c (spec S2.9, and bk7258_armino_sta.c's own header:
 * "the connection entry is to become wlan0's wireless ops, so that wapi
 * drives it").
 *
 * NuttX owns the contract and nothing below it: `struct iwreq`'s layout,
 * the SIOCSIWxxx/SIOCGIWxxx ioctl set, and the dispatch in
 * netdev_upper_wireless_ioctl() (which even chains essid -> connect for
 * us).  NuttX does not know how this chip associates or scans; that is
 * what this file supplies, by calling Armino's own entry points.
 *
 * Behaviour is copied from chips/bk7258/wifi_armino/bk7258_armino_sta.c,
 * which is the sequence a7de5d2 connected with on the board:
 *
 *   connect    = demo_sta_app_init(ssid, password)
 *                (= bk_wifi_sta_set_config() + bk_wifi_sta_start(),
 *                 wifi_api.c:183)
 *   disconnect = bk_wifi_sta_disconnect() then bk_wifi_sta_stop()
 *                (wifi.h:97/:103: stop alone does not fire
 *                 EVENT_WIFI_STA_DISCONNECTED, and a restart needs the
 *                 STARTED bit cleared first -- wifi_v2.c:2429
 *                 "sta already started, ignored!")
 *
 * CONFIG_CONNECT_THROUGH_PSK_OR_SAE_PASSWORD is not set in the baseline
 * sdkconfig, so demo_sta_app_init() is the two-argument form
 * (bk_private/bk_wifi.h:503).
 *
 * Credentials never leave this file: SIOCGIWENCODEEXT is refused rather
 * than echoed back, so no tool can read the passphrase out of the device.
 *
 * Registered from bk7258_armino_netdev.c, which sets lower.iw_ops to
 * g_armino_iw_ops.
 *
 * Cross-core transport limit.  On the AP these ioctls reach CP through
 * usrsock-over-RPMsg, and one RPMsg buffer is RPMSG_BUFFER_SIZE = 512
 * bytes (openamp/rpmsg_virtio.h:27).  A pointer ioctl sends the iwreq AND
 * its pointee as one request (net/usrsock/usrsock_ioctl.c:141), so
 * SIOCGIWSCAN with wapi's CONFIG_WIRELESS_WAPI_SCAN_MAX_DATA buffer is
 * longer than one buffer and gets split.  The server only reassembles
 * sendto (drivers/usrsock/usrsock_rpmsg_server.c: usrsock_rpmsg_ept_do_cb
 * has `if (uept->remain > 0)` for that one case), so every later fragment
 * is parsed as a fresh request -- the `psock_socket: address family
 * unsupported: 0` and the RPMSG_ASSERT at rpmsg_virtio.c:249 on the board
 * are that.  Until the server reassembles ioctls too (an upstream change),
 * the scan buffer is kept under one RPMsg worth and this file truncates
 * rather than answering E2BIG, because wapi doubles its buffer on E2BIG
 * and the retry only pushes the request further over the limit.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <net/if_arp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kthread.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/wireless/wireless.h>

#include <modules/wifi.h>
#include <modules/wifi_types.h>

/* bk_private/bk_wifi.h:503 -- two-argument form in this configuration. */

int demo_sta_app_init(char *oob_ssid, char *connect_key);

/* Cache for the two halves of a connect.  netdev_upper_wireless_ioctl()
 * delivers essid and passwd as separate ioctls and then calls connect();
 * demo_sta_app_init() wants both at once, so hold them between the two.
 */

static char s_ssid[WIFI_SSID_STR_LEN];
static char s_pass[WIFI_PASSWORD_LEN];

/* wapi psk <ifname> <key> [alg] sends SIOCSIWAUTH twice (IW_AUTH_WPA_VERSION,
 * IW_AUTH_CIPHER_PAIRWISE) before SIOCSIWENCODEEXT.  demo_sta_app_init() infers
 * the security type from the key itself, so the auth params are accepted and
 * remembered rather than forwarded.
 */

static uint32_t s_auth_wpa_version;
static uint32_t s_auth_cipher_pairwise;

/* A scan only yields results after a scan was started here.  Without this
 * guard a bare `wapi scan_results` polls bk_wifi_scan_get_result() for as long
 * as wapi's loop runs, each poll an RPMsg round trip and each call an Armino
 * error (-5151), which is what wedged the transport on the board.
 */

static bool s_scan_started;

/* wext keeps scan results queryable until the next scan.  Armino's
 * bk_wifi_scan_get_result() hands over the list once, so the packed stream
 * is cached here and served to every later SIOCGIWSCAN; the cache is
 * dropped when a new scan is started.
 */

#define WOPS_SCAN_CACHE_MAX  512

static uint8_t s_scan_cache[WOPS_SCAN_CACHE_MAX];
static uint16_t s_scan_cache_len;

static int wops_essid(FAR struct netdev_lowerhalf_s *dev,
                      FAR struct iwreq *iwr, bool set)
{
  FAR struct iw_point *p = &iwr->u.essid;

  (void)dev;

  if (set)
    {
      if (p->pointer == NULL || p->length == 0 ||
          p->length > WIFI_SSID_STR_LEN - 1)
        {
          return -EINVAL;
        }

      memset(s_ssid, 0, sizeof(s_ssid));
      memcpy(s_ssid, p->pointer, p->length);
      return 0;
    }

  {
    size_t n = strlen(s_ssid);

    if (p->pointer != NULL && p->length > 0)
      {
        size_t take = n < p->length ? n : p->length;
        memcpy(p->pointer, s_ssid, take);
      }

    p->length = (uint16_t)n;
    return 0;
  }
}

static int wops_passwd(FAR struct netdev_lowerhalf_s *dev,
                       FAR struct iwreq *iwr, bool set)
{
  FAR struct iw_point *p = &iwr->u.data;
  FAR struct iw_encode_ext *ext;

  (void)dev;

  if (!set)
    {
      /* Never hand a passphrase back to userspace. */

      return -EACCES;
    }

  if (p->pointer == NULL || p->length < sizeof(*ext))
    {
      return -EINVAL;
    }

  /* SIOCSIWENCODEEXT carries struct iw_encode_ext followed by the key bytes
   * (apps/wireless/wapi/src/driver_wext.c:165-180: malloc(sizeof(*ext) +
   * key_len), key copied to ext + 1).  The plaintext is ext->key, not the
   * whole buffer.
   */

  ext = (FAR struct iw_encode_ext *)p->pointer;
  if (ext->key_len == 0)
    {
      s_pass[0] = '\0';
      return 0;
    }

  if (ext->key_len > WIFI_PASSWORD_LEN - 1 ||
      p->length < sizeof(*ext) + ext->key_len)
    {
      return -E2BIG;
    }

  memset(s_pass, 0, sizeof(s_pass));
  memcpy(s_pass, ext->key, ext->key_len);
  return 0;
}

static int wops_auth(FAR struct netdev_lowerhalf_s *dev,
                     FAR struct iwreq *iwr, bool set)
{
  FAR struct iw_param *param = &iwr->u.param;

  (void)dev;

  if (!set)
    {
      switch (param->flags & IW_AUTH_INDEX)
        {
          case IW_AUTH_WPA_VERSION:
            param->value = (int32_t)s_auth_wpa_version;
            return 0;
          case IW_AUTH_CIPHER_PAIRWISE:
            param->value = (int32_t)s_auth_cipher_pairwise;
            return 0;
          default:
            return -ENOTTY;
        }
    }

  switch (param->flags & IW_AUTH_INDEX)
    {
      case IW_AUTH_WPA_VERSION:
        s_auth_wpa_version = (uint32_t)param->value;
        return 0;
      case IW_AUTH_CIPHER_PAIRWISE:
        s_auth_cipher_pairwise = (uint32_t)param->value;
        return 0;
      default:
        /* Algs, key management, etc. are inferred by demo_sta_app_init(). */

        return 0;
    }
}

/* Connect and disconnect run Armino's own STA entry, which is deep and can
 * take seconds.  They are dispatched to a thread of their own rather than
 * run in the caller: the caller is usrsock_rpmsg_worker() on the RPMsg
 * work queue (net/usrsock + drivers/usrsock/usrsock_rpmsg_server.c:978),
 * and CONFIG_SCHED_HPWORKSTACKSIZE is 2048 on CP -- a stack overflow on
 * the board, and while it runs no other usrsock ack can be sent, so the
 * RPMsg TX pool drains.  armino_sta never had this problem because it is
 * an NSH command with a 4096-byte stack.
 *
 * The ioctls therefore report success and the association continues in the
 * background; the address shows up on wlan0 through the usual
 * EVENT_WIFI_STA_CONNECTED -> sta_ip_start() -> DHCP path.
 */

#define WOPS_WORKER_STACKSIZE   8192
#define WOPS_WORKER_PRIORITY    200

enum wops_job_e
{
  WOPS_JOB_IDLE,
  WOPS_JOB_CONNECT,
  WOPS_JOB_DISCONNECT,
};

/* One job at a time: the netdev upper half serialises the iw_ops calls
 * that produce them, and a second connect while one is running would only
 * be a wifi_v2.c:2429 "sta already started" error anyway.
 */

static struct
{
  enum wops_job_e what;
  char ssid[WIFI_SSID_STR_LEN];
  char pass[WIFI_PASSWORD_LEN];
} s_job;

static int wops_worker(int argc, FAR char *argv[])
{
  (void)argc;
  (void)argv;

  switch (s_job.what)
    {
      case WOPS_JOB_CONNECT:
        demo_sta_app_init(s_job.ssid, s_job.pass);
        break;

      case WOPS_JOB_DISCONNECT:
        /* Order matters: disconnect fires EVENT_WIFI_STA_DISCONNECTED
         * (which drives sta_ip_down()), stop clears STARTED so a later
         * connect does not hit wifi_v2.c:2429 "sta already started,
         * ignored!".
         */

        bk_wifi_sta_disconnect();
        bk_wifi_sta_stop();
        break;

      default:
        break;
    }

  s_job.what = WOPS_JOB_IDLE;
  return 0;
}

static int wops_dispatch(enum wops_job_e what)
{
  pid_t pid;

  if (s_job.what != WOPS_JOB_IDLE)
    {
      return -EBUSY;
    }

  s_job.what = what;
  pid = kthread_create("armino-wops", WOPS_WORKER_PRIORITY,
                       WOPS_WORKER_STACKSIZE, wops_worker, NULL);
  if (pid < 0)
    {
      s_job.what = WOPS_JOB_IDLE;
      return pid;
    }

  return 0;
}

static int wops_connect(FAR struct netdev_lowerhalf_s *dev)
{
  (void)dev;

  strlcpy(s_job.ssid, s_ssid, sizeof(s_job.ssid));
  strlcpy(s_job.pass, s_pass, sizeof(s_job.pass));
  return wops_dispatch(WOPS_JOB_CONNECT);
}

static int wops_disconnect(FAR struct netdev_lowerhalf_s *dev)
{
  (void)dev;

  return wops_dispatch(WOPS_JOB_DISCONNECT);
}

static int wops_mode(FAR struct netdev_lowerhalf_s *dev,
                     FAR struct iwreq *iwr, bool set)
{
  (void)dev;

  if (set)
    {
      return iwr->u.mode == IW_MODE_INFRA ? 0 : -EOPNOTSUPP;
    }

  iwr->u.mode = IW_MODE_INFRA;
  return 0;
}

/****************************************************************************
 * Scan
 *
 * set=1 starts a scan.  set=0 drains the results into the wext event
 * stream wapi_scan_coll() reads (apps/wireless/wapi/src/wireless.c:1400).
 * wapi_event_stream_extract() walks items by iwe->len, and for point
 * events (SIOCGIWESSID/SIOCGIWENCODE/IWEVGENIE) treats u.data.pointer as
 * a byte offset from &iwe->u to the payload -- so a point event is
 * [iw_event header][payload] with pointer = sizeof(union iwreq_data).
 * The tags emitted are the ones wapi_scan_event() recognises:
 * SIOCGIWAP, SIOCGIWESSID, SIOCGIWFREQ, IWEVQUAL, SIOCGIWENCODE.
 ****************************************************************************/

#define WOPS_POINT_DATA_OFF  (uint16_t)sizeof(union iwreq_data)

/* Fixed-payload event: the whole iw_event is the item. */

static int wops_emit_fixed(uint8_t **cursor, uint8_t *end,
                           const struct iw_event *src)
{
  if (*cursor + sizeof(struct iw_event) > end)
    {
      return -E2BIG;
    }

  memcpy(*cursor, src, sizeof(struct iw_event));
  *cursor += sizeof(struct iw_event);
  return 0;
}

/* Point event: [iw_event header][payload], u.data.pointer is an offset
 * from &iwe->u to the payload.
 */

static int wops_emit_point(uint8_t **cursor, uint8_t *end, uint16_t cmd,
                           const void *payload, uint16_t payload_len)
{
  struct iw_event iwe;
  uint16_t total = (uint16_t)(sizeof(struct iw_event) + payload_len);

  if (*cursor + total > end)
    {
      return -E2BIG;
    }

  memset(&iwe, 0, sizeof(iwe));
  iwe.cmd = cmd;
  iwe.len = total;
  iwe.u.data.pointer = (FAR void *)(uintptr_t)WOPS_POINT_DATA_OFF;
  iwe.u.data.length = payload_len;

  memcpy(*cursor, &iwe, sizeof(iwe));
  if (payload_len > 0)
    {
      memcpy(*cursor + sizeof(iwe), payload, payload_len);
    }

  *cursor += total;
  return 0;
}

static int wops_scan(FAR struct netdev_lowerhalf_s *dev,
                     FAR struct iwreq *iwr, bool set)
{
  (void)dev;

  if (set)
    {
      wifi_scan_config_t cfg;

      memset(&cfg, 0, sizeof(cfg));
      cfg.scan_type = 0;   /* active */
      if (bk_wifi_scan_start(&cfg) < 0)
        {
          return -EIO;
        }

      s_scan_started = true;
      s_scan_cache_len = 0;
      return 0;
    }

  if (!s_scan_started)
    {
      /* Nothing to report and nothing to poll for.  Answering "empty" is
       * better than an EAGAIN loop over RPMsg: wapi_scan_stat() polls
       * SIOCGIWSCAN until it sees E2BIG or data, and each poll is a full
       * cross-core round trip.
       */

      iwr->u.data.length = 0;
      return 0;
    }

  if (s_scan_cache_len > 0)
    {
      uint16_t take = s_scan_cache_len < iwr->u.data.length ?
                      s_scan_cache_len : iwr->u.data.length;

      memcpy(iwr->u.data.pointer, s_scan_cache, take);
      iwr->u.data.length = take;
      return 0;
    }

  if (iwr->u.data.length == 0)
    {
      /* Zero-length probe: wapi_scan_stat() sends this to ask whether
       * results are ready.  E2BIG is the wext "data ready, no room" answer
       * it looks for (wireless.c:1303) and it moves no payload, so it is
       * safe across the RPMsg link.
       */

      return -E2BIG;
    }

  {
    wifi_scan_result_t res;
    FAR uint8_t *cursor = iwr->u.data.pointer;
    FAR uint8_t *end = cursor + iwr->u.data.length;
    int i;

    memset(&res, 0, sizeof(res));
    if (bk_wifi_scan_get_result(&res) < 0)
      {
        return -EAGAIN;
      }

    for (i = 0; i < res.ap_num && res.aps != NULL; i++)
      {
        const wifi_scan_ap_info_t *ap = &res.aps[i];
        struct iw_event iwe;

        /* BSSID */

        memset(&iwe, 0, sizeof(iwe));
        iwe.cmd = SIOCGIWAP;
        iwe.len = sizeof(iwe);
        iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwe.u.ap_addr.sa_data, ap->bssid, 6);
        if (wops_emit_fixed(&cursor, end, &iwe) < 0)
          {
            goto overflow;
          }

        /* SSID */

        if (wops_emit_point(&cursor, end, SIOCGIWESSID, ap->ssid,
                            (uint16_t)strnlen(ap->ssid,
                                              WIFI_SSID_STR_LEN)) < 0)
          {
            goto overflow;
          }

        /* Channel as a frequency entry: m = channel, e = 0. */

        memset(&iwe, 0, sizeof(iwe));
        iwe.cmd = SIOCGIWFREQ;
        iwe.len = sizeof(iwe);
        iwe.u.freq.m = ap->channel;
        if (wops_emit_fixed(&cursor, end, &iwe) < 0)
          {
            goto overflow;
          }

        /* Quality: dBm level. */

        memset(&iwe, 0, sizeof(iwe));
        iwe.cmd = IWEVQUAL;
        iwe.len = sizeof(iwe);
        iwe.u.qual.level = (uint8_t)ap->rssi;
        iwe.u.qual.updated = IW_QUAL_QUAL_INVALID | IW_QUAL_DBM;
        if (wops_emit_fixed(&cursor, end, &iwe) < 0)
          {
            goto overflow;
          }

        /* Encryption on/off. */

        memset(&iwe, 0, sizeof(iwe));
        iwe.cmd = SIOCGIWENCODE;
        iwe.len = sizeof(iwe);
        iwe.u.data.flags = (ap->security == WIFI_SECURITY_NONE) ?
                           IW_ENCODE_OPEN : IW_ENCODE_RESTRICTED;
        if (wops_emit_fixed(&cursor, end, &iwe) < 0)
          {
            goto overflow;
          }
      }

    bk_wifi_scan_free_result(&res);
    {
      uint16_t used =
        (uint16_t)(cursor - (FAR uint8_t *)iwr->u.data.pointer);

      memcpy(s_scan_cache, iwr->u.data.pointer, used);
      s_scan_cache_len = used;
      iwr->u.data.length = used;
    }

    return 0;

  overflow:
    /* Truncate rather than answer E2BIG.  wapi_scan_coll() doubles its
     * buffer and retries on E2BIG, and a bigger buffer only makes the
     * next request longer than one RPMsg buffer -- which the usrsock
     * server cannot reassemble (see the note in the file header).  A
     * partial list is more use than a dead board.
     */

    bk_wifi_scan_free_result(&res);
    {
      uint16_t used =
        (uint16_t)(cursor - (FAR uint8_t *)iwr->u.data.pointer);

      memcpy(s_scan_cache, iwr->u.data.pointer, used);
      s_scan_cache_len = used;
      iwr->u.data.length = used;
    }

    return 0;
  }
}

static int wops_range(FAR struct netdev_lowerhalf_s *dev,
                      FAR struct iwreq *iwr)
{
  (void)dev;
  (void)iwr;
  return -ENOTTY;
}

const struct wireless_ops_s g_armino_iw_ops =
{
  .connect    = wops_connect,
  .disconnect = wops_disconnect,
  .essid      = wops_essid,
  .passwd     = wops_passwd,
  .auth       = wops_auth,
  .mode       = wops_mode,
  .scan       = wops_scan,
  .range      = wops_range,
};
