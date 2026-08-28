/*
 * chips/bk7258/wifi/bk7258_wifi_internal.h
 *
 * Team-owned BK7258 Wi-Fi skeleton internal contract.
 *
 * This header declares the interfaces implemented by the team-owned Wi-Fi
 * skeleton. It intentionally includes no Armino/Beken source; the vendor
 * MAC/PHY/RF runtime is an external input delivered via a patch manifest
 * (see beken_patch_manifest.md). Do not copy Armino headers into this tree.
 */

#ifndef __CHIPS_BK7258_WIFI_BK7258_WIFI_INTERNAL_H
#define __CHIPS_BK7258_WIFI_BK7258_WIFI_INTERNAL_H

#include <nuttx/config.h>
#include <nuttx/mutex.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/mm/iob.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Vendor packet ABI constants
 *
 * Locked beken_genie BK7258 profile private reserve (plan §11.4.2). These are
 * versioned inputs, not permanent silicon constants; the ABI audit must pin
 * them to the vendor library/firmware hash.
 ****************************************************************************/

/* Private reserve is sourced from Kconfig so the vendor profile value lives
 * in exactly one place; the ABI audit pins it to the library/firmware hash. */
#define BK7258_WIFI_PRIVATE_RESERVE \
  (CONFIG_BK7258_WIFI_MSDU_RESV_HEAD_LENGTH + \
   CONFIG_BK7258_WIFI_MSDU_RESV_DESC_LENGTH)

/* Ethernet II frame we must carry for DHCP/ARP/MTU 1500. */
#define BK7258_WIFI_MTU         1500u
#define BK7258_WIFI_FRAME_MAX   (BK7258_WIFI_MTU + 14u)

/* Scan results have SDK-owned storage only until the scan-done callback
 * returns.  Keep a bounded, NuttX-owned snapshot for WEXT result queries.
 * This is deliberately a scan-only cache: it contains no credentials or
 * association state. */

#define BK7258_WIFI_SCAN_MAX_APS    32u
#define BK7258_WIFI_SCAN_SSID_LEN   32u

struct bk7258_wifi_scan_ap_s
{
  char     ssid[BK7258_WIFI_SCAN_SSID_LEN + 1u];
  uint8_t  bssid[6];
  int      rssi;
  uint8_t  channel;
  uint32_t security;
};

/****************************************************************************
 * Vendor packet
 *
 * Minimal team-owned packet container whose layout satisfies the Beken MAC
 * data-path contract (next/payload/len/tot_len plus a private reserve that
 * holds sk_buff + tx descriptor). It is NOT a NuttX netpkt and MUST NOT be
 * passed where a netpkt_t is expected. Field order must remain layout
 * compatible with the vendor struct pbuf used by the Beken integration
 * branch; this is re-verified in ABI_AUDIT.md.
 ****************************************************************************/

struct bk7258_vpkt
{
  struct bk7258_vpkt *next;
  uint8_t           *payload;
  uint16_t           tot_len;
  uint16_t           len;
  uint16_t           ref;
  uint8_t            flags;
  uint8_t            type_internal;
  /* Data follows: BK7258_WIFI_PRIVATE_RESERVE + payload capacity. */
};

/* Packet type bits, mirroring the vendor contract without importing it. */
#define BK7258_VPKT_FLAG_RX    0x01u
#define BK7258_VPKT_FLAG_EXT   0x02u

/****************************************************************************
 * OSAL
 *
 * Thin wrappers over NuttX primitives. The Beken integration branch maps its
 * wifi_os_funcs_t callbacks onto these functions; nothing here may create a
 * second scheduler. Each wrapper documents its blocking/context contract.
 ****************************************************************************/

int  bk7258_wifi_osal_init(void);
void bk7258_wifi_osal_deinit(void);

int  bk7258_wifi_osal_thread_create(uintptr_t *handle, int prio,
                                    const char *name,
                                    void (*entry)(void *arg), void *arg,
                                    size_t stack_size);
int  bk7258_wifi_osal_thread_delete(uintptr_t handle);

int  bk7258_wifi_osal_queue_create(uintptr_t *handle, const char *name,
                                   size_t msg_size, size_t msg_count);
int  bk7258_wifi_osal_queue_send(uintptr_t handle, const void *msg,
                                 unsigned int timeout_ms);
int  bk7258_wifi_osal_queue_recv(uintptr_t handle, void *msg,
                                 unsigned int timeout_ms);
bool bk7258_wifi_osal_queue_empty(uintptr_t handle);
bool bk7258_wifi_osal_queue_full(uintptr_t handle);
int  bk7258_wifi_osal_queue_send_front(uintptr_t handle, const void *msg,
                                       unsigned int timeout_ms);
int  bk7258_wifi_osal_queue_delete(uintptr_t handle);

int  bk7258_wifi_osal_mutex_create(uintptr_t *handle);
int  bk7258_wifi_osal_mutex_lock(uintptr_t handle);
int  bk7258_wifi_osal_mutex_unlock(uintptr_t handle);
int  bk7258_wifi_osal_mutex_delete(uintptr_t handle);

int  bk7258_wifi_osal_sem_create(uintptr_t *handle, unsigned int max_count);
int  bk7258_wifi_osal_sem_wait(uintptr_t handle, unsigned int timeout_ms);
int  bk7258_wifi_osal_sem_post(uintptr_t handle);
int  bk7258_wifi_osal_sem_delete(uintptr_t handle);

void bk7258_wifi_osal_delay_ms(uint32_t ms);
void bk7258_wifi_osal_delay_us(uint32_t us);

void *bk7258_wifi_osal_malloc(size_t size);
void *bk7258_wifi_osal_zalloc(size_t size);
void *bk7258_wifi_osal_realloc(void *ptr, size_t size);
void  bk7258_wifi_osal_free(void *ptr);

uint32_t bk7258_wifi_osal_enter_critical(void);
void     bk7258_wifi_osal_exit_critical(uint32_t flags);
uint32_t bk7258_wifi_osal_disable_irq(void);
void     bk7258_wifi_osal_enable_irq(uint32_t level);

uint64_t bk7258_wifi_osal_time_ms(void);

/****************************************************************************
 * Vendor packet allocator
 ****************************************************************************/

struct bk7258_vpkt *bk7258_vpkt_alloc(uint16_t len, bool rx);
struct bk7258_vpkt *bk7258_vpkt_alloc_ref(void *payload, uint16_t len);
void  bk7258_vpkt_ref(struct bk7258_vpkt *p);
void  bk7258_vpkt_free(struct bk7258_vpkt *p);
int   bk7258_vpkt_push(struct bk7258_vpkt *p, int16_t delta);
void *bk7258_vpkt_private_area(struct bk7258_vpkt *p);
int   bk7258_vpkt_coalesce(struct bk7258_vpkt **p);

/****************************************************************************
 * lower-half data/control plane
 ****************************************************************************/

/* Private per-device state. The lower half is embedded first so the public
 * netdev_lowerhalf_s pointer equals the object pointer. */
struct bk7258_wifi_s
{
  struct netdev_lowerhalf_s lower;
  uintptr_t osal_mutex;
  uint8_t mac[6];
  uint8_t vif_idx;
  bool carrier;
  bool registered;
  struct iob_s *rx_head;
  struct iob_s *rx_tail;

  /* Accessed by the vendor scan-done callback and NuttX WEXT ioctl callers.
   * The callback never retains SDK-owned scan_result.aps memory. */

  mutex_t scan_lock;
  bool scan_lock_ready;
  bool scan_callback_registered;
  bool scan_in_progress;
  bool scan_complete;
  int scan_status;
  uint32_t scan_id;
  uint8_t scan_count;
  struct bk7258_wifi_scan_ap_s scan_aps[BK7258_WIFI_SCAN_MAX_APS];
};

int bk7258_wifi_lower_init(struct bk7258_wifi_s *priv);
int bk7258_wifi_lower_uninit(struct bk7258_wifi_s *priv);
int bk7258_wifi_lower_register(struct bk7258_wifi_s *priv);
void bk7258_wifi_lower_carrier_on(struct bk7258_wifi_s *priv);
void bk7258_wifi_lower_carrier_off(struct bk7258_wifi_s *priv);
void bk7258_wifi_lower_rx_ready(struct bk7258_wifi_s *priv);
void bk7258_wifi_lower_tx_done(struct bk7258_wifi_s *priv);

/* Submitted by the vendor RX path; the lower half copies into a NetPKT. */
void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt);

/****************************************************************************
 * HW glue (clock/power/IRQ)
 ****************************************************************************/

int  bk7258_wifi_hw_init(void);
void bk7258_wifi_hw_deinit(void);
int  bk7258_wifi_hw_power_on(void);
int  bk7258_wifi_hw_power_off(void);
int  bk7258_wifi_hw_clock_on(void);
int  bk7258_wifi_hw_clock_off(void);

/****************************************************************************
 * Board configuration
 ****************************************************************************/

int  bk7258_wifi_board_init(void);
int  bk7258_wifi_board_get_mac(uint8_t mac[6]);

/****************************************************************************
 * Public entry
 ****************************************************************************/

int bk7258_wifi_initialize(void);

/* True once bk7258_wifi_initialize() has completed (lower half registered).
 * Lets command entry points auto-initialize instead of silently running a
 * scan against an unpowered MAC/PHY domain. */
bool bk7258_wifi_is_ready(void);

#endif /* __CHIPS_BK7258_WIFI_BK7258_WIFI_INTERNAL_H */
