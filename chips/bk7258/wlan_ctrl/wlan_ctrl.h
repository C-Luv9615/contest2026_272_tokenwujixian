/****************************************************************************
 * chips/bk7258/wlan_ctrl/wlan_ctrl.h
 *
 * Cross-core Wi-Fi control plane.
 *
 * The wlan0 netdev and the Armino STA both live on CP.  Applications live
 * on AP (the UI, ai_agent, later MCP) and need to trigger a scan, read the
 * results as data, and connect/disconnect.  Sending that over usrsock was
 * the wrong shape -- usrsock is a socket proxy and the wext pointee
 * travelled with the request, which the RPMsg transport cannot reassemble
 * (RPMSG_BUFFER_SIZE is 512 and only sendto fragments are stitched back
 * together).  This is a control channel of its own instead, on
 * rpmsg_char: both cores call rpmsg_char_create_eptdev("wlan-ctrl") and
 * each gets a local /dev/rpmsg-wlan-ctrl whose read/write is one message
 * at a time.
 *
 * Nothing here reaches into nuttx/ -- rpmsg_char is already there, the
 * rest is chips/bk7258 and app code.
 *
 * One AP per message: a scan of 19 networks is 19 small messages rather
 * than one large one, so no message has to be reassembled.
 *
 * Usage (AP):
 *     struct wifi_ap_info_t aps[32];
 *     int n = 0;
 *     wifi_ctrl_scan_start();
 *     ... wait a second ...
 *     wifi_ctrl_scan_get(aps, 32, &n);
 *     wifi_ctrl_connect(aps[i].ssid, "passphrase");
 *     wifi_ctrl_disconnect();
 *
 * The same operations are reachable as wext ioctls through
 * compat/bk7258_armino_wops.c; both faces call the same implementation in
 * wlan_ctrl_server.c, so the Armino call order documented there is the
 * only copy.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_WLAN_CTRL_H
#define __CHIPS_BK7258_WLAN_CTRL_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#define WLAN_CTRL_NAME        "wlan-ctrl"
#define WLAN_CTRL_PATH        "/dev/rpmsg-" WLAN_CTRL_NAME

/* Explicit endpoint addresses rather than RPMSG_ADDR_ANY.  With ANY,
 * rpmsg_char_create_eptdev() starts the "wait for bind" semaphore at 0 and
 * every blocking write parks in rpmsg_wait() until the nameservice pairs
 * the two ends -- which never happened here, so `wapi` hung in write()
 * with the read timeout never reached.  Fixed addresses bind immediately
 * and the semaphore starts at 1 (rpmsg_char.c: rpmsg_char_create_eptdev).
 * Chosen clear of the endpoints already on the board (53, 1024..1028).
 */

#define WLAN_CTRL_ADDR_CP     0x0c10
#define WLAN_CTRL_ADDR_AP     0x0c11

#define WLAN_CTRL_SSID_LEN    33    /* WIFI_SSID_STR_LEN  */
#define WLAN_CTRL_PASS_LEN    65    /* WIFI_PASSWORD_LEN  */

/* One AP, as the UI wants it. */

struct wifi_ap_info_s
{
  char     ssid[WLAN_CTRL_SSID_LEN];
  uint8_t  bssid[6];
  int8_t   rssi;
  uint8_t  channel;
  bool     secured;
};

enum wlan_ctrl_op_e
{
  WLAN_CTRL_OP_SCAN_START = 1,   /* begin a scan                       */
  WLAN_CTRL_OP_SCAN_GET,         /* read results back, one AP a message */
  WLAN_CTRL_OP_CONNECT,          /* ssid + pass in the request          */
  WLAN_CTRL_OP_DISCONNECT,
  WLAN_CTRL_OP_STATUS,
};

/* The wire format.  One struct, one message -- rpmsg_char's read/write is
 * message oriented, so this never needs reassembling.
 */

struct wlan_ctrl_msg_s
{
  uint16_t op;                   /* enum wlan_ctrl_op_e           */
  int16_t  result;               /* 0 or negative errno           */
  uint8_t  more;                 /* SCAN_GET: another message follows */
  uint8_t  _pad;
  uint16_t total;                /* SCAN_GET reply: APs in the list   */
  uint16_t index;                /* SCAN_GET request: which AP wanted */
  char     ssid[WLAN_CTRL_SSID_LEN];
  char     pass[WLAN_CTRL_PASS_LEN];
  struct wifi_ap_info_s ap;      /* SCAN_GET payload                  */
};

/****************************************************************************
 * AP side: the client.  wlan_ctrl_client.c
 ****************************************************************************/

int wifi_ctrl_scan_start(void);
int wifi_ctrl_scan_get(FAR struct wifi_ap_info_s *aps, int max, int *out_n);
int wifi_ctrl_connect(FAR const char *ssid, FAR const char *pass);
int wifi_ctrl_disconnect(void);
int wifi_ctrl_status(FAR struct wifi_ap_info_s *ap, bool *connected);

/****************************************************************************
 * CP side: the operations, shared with the wext face in
 * compat/bk7258_armino_wops.c.  wlan_ctrl_server.c
 ****************************************************************************/

int bk7258_wlan_scan_start(void);
int bk7258_wlan_connect(FAR const char *ssid, FAR const char *pass);
int bk7258_wlan_disconnect(void);

/* Bring up the CP half: register the rpmsg_char endpoint and start the
 * request loop.  Called from board_late_initialize().
 */

int wlan_ctrl_server_init(void);

#endif /* __CHIPS_BK7258_WLAN_CTRL_H */
