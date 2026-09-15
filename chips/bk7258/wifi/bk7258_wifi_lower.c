/*
 * chips/bk7258/wifi/bk7258_wifi_lower.c
 *
 * NuttX netdev_lowerhalf data/control plane for BK7258 Wi-Fi (STA MVP).
 *
 * Skeleton status: the ops tables and register/carrier plumbing are wired;
 * transmit/receive and the wireless_ops_s handlers return documented
 * errors until the Beken public API (bk_wifi_sta_*, bk_wifi_scan_*) and the
 * vendor MAC TX/RX entry points are connected by the integration branch.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netdev_lowerhalf.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

#include "bk7258_wifi_internal.h"

/****************************************************************************
 * Private data
 ****************************************************************************/

static struct bk7258_wifi_s g_bk7258_wifi;

#define PRIV2LOWER(p) (&(p)->lower)

/****************************************************************************
 * Data plane
 *
 * transmit/receive stay disconnected until the vendor MAC TX/RX entry points
 * are wired. Returning -ENOSYS from transmit makes the upper half recycle the
 * NetPKT; returning NULL from receive signals "no packet available".
 ****************************************************************************/

static int bk7258_wifi_ifup(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  /* Skeleton: administrative up only. Carrier stays off until the vendor
   * authentication event is connected. */
  priv->carrier = false;
  return OK;
}

static int bk7258_wifi_ifdown(FAR struct netdev_lowerhalf_s *lower)
{
  FAR struct bk7258_wifi_s *priv =
    (FAR struct bk7258_wifi_s *)lower;

  priv->carrier = false;
  return OK;
}

static int bk7258_wifi_transmit(FAR struct netdev_lowerhalf_s *lower,
                                FAR netpkt_t *pkt)
{
  /* Not wired: vendor MAC TX (bmsg_tx_sender via the integration branch) is
   * required before a frame can be submitted. */
  return -ENOSYS;
}

static FAR netpkt_t *bk7258_wifi_receive(FAR struct netdev_lowerhalf_s *lower)
{
  /* Not wired: RX NetPKT queue is populated by bk7258_wifi_lower_rx_submit
   * once the vendor RX path delivers Ethernet frames. */
  return NULL;
}

static void bk7258_wifi_reclaim(FAR struct netdev_lowerhalf_s *lower)
{
  /* Nothing queued at skeleton stage; a real TX queue would walk and free
   * completed descriptors here. */
}

/****************************************************************************
 * Control plane
 *
 * Each handler returns -ENOSYS until the matching Beken public API is
 * connected. ESSID/passwd/auth are cached then submitted atomically by
 * connect(); no handler may log a passphrase.
 ****************************************************************************/

static int bk7258_wifi_connect(FAR struct netdev_lowerhalf_s *lower)
{
  return -ENOSYS;
}

static int bk7258_wifi_disconnect(FAR struct netdev_lowerhalf_s *lower)
{
  return -ENOSYS;
}

static int bk7258_wifi_essid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_bssid(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_passwd(FAR struct netdev_lowerhalf_s *lower,
                              FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_mode(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_auth(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_country(FAR struct netdev_lowerhalf_s *lower,
                               FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_scan(FAR struct netdev_lowerhalf_s *lower,
                            FAR struct iwreq *iwr, bool set)
{
  return -ENOSYS;
}

static int bk7258_wifi_range(FAR struct netdev_lowerhalf_s *lower,
                             FAR struct iwreq *iwr)
{
  return -ENOSYS;
}

/****************************************************************************
 * Ops tables
 ****************************************************************************/

static const struct netdev_ops_s g_bk7258_net_ops =
{
  .ifup     = bk7258_wifi_ifup,
  .ifdown   = bk7258_wifi_ifdown,
  .transmit = bk7258_wifi_transmit,
  .receive  = bk7258_wifi_receive,
  .reclaim  = bk7258_wifi_reclaim,
};

static const struct wireless_ops_s g_bk7258_iw_ops =
{
  .connect    = bk7258_wifi_connect,
  .disconnect = bk7258_wifi_disconnect,
  .essid      = bk7258_wifi_essid,
  .bssid      = bk7258_wifi_bssid,
  .passwd     = bk7258_wifi_passwd,
  .mode       = bk7258_wifi_mode,
  .auth       = bk7258_wifi_auth,
  .country    = bk7258_wifi_country,
  .scan       = bk7258_wifi_scan,
  .range      = bk7258_wifi_range,
};

/****************************************************************************
 * Lifecycle / registration
 ****************************************************************************/

int bk7258_wifi_lower_init(struct bk7258_wifi_s *priv)
{
  memset(priv, 0, sizeof(*priv));
  return 0;
}

int bk7258_wifi_lower_uninit(struct bk7258_wifi_s *priv)
{
  if (priv->registered)
    {
      netdev_lower_unregister(PRIV2LOWER(priv));
      priv->registered = false;
    }

  return 0;
}

int bk7258_wifi_lower_register(struct bk7258_wifi_s *priv)
{
  int ret;

  priv->lower.ops = &g_bk7258_net_ops;
#ifdef CONFIG_NETDEV_WIRELESS_HANDLER
  priv->lower.iw_ops = &g_bk7258_iw_ops;
#endif
  atomic_init(&priv->lower.quota[NETPKT_TX], 1);
  atomic_init(&priv->lower.quota[NETPKT_RX], 1);

  ret = netdev_lower_register(PRIV2LOWER(priv), NET_LL_IEEE80211);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  priv->carrier = false;
  return OK;
}

void bk7258_wifi_lower_carrier_on(struct bk7258_wifi_s *priv)
{
  priv->carrier = true;
  netdev_lower_carrier_on(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_carrier_off(struct bk7258_wifi_s *priv)
{
  priv->carrier = false;
  netdev_lower_carrier_off(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_rx_ready(struct bk7258_wifi_s *priv)
{
  netdev_lower_rxready(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_tx_done(struct bk7258_wifi_s *priv)
{
  netdev_lower_txdone(PRIV2LOWER(priv));
}

void bk7258_wifi_lower_rx_submit(struct bk7258_wifi_s *priv,
                                 struct bk7258_vpkt *vpkt)
{
  /* Skeleton: vendor RX Ethernet frames must be copied into an RX NetPKT and
   * queued here, then netdev_lower_rxready() notified. EAPOL/WAI is diverted
   * by the vendor path before reaching this function. Not wired yet. */
  (void)priv;
  bk7258_vpkt_free(vpkt);
}

/****************************************************************************
 * Public entry
 ****************************************************************************/

int bk7258_wifi_initialize(void)
{
  int ret;

  ret = bk7258_wifi_osal_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_hw_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_board_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_lower_init(&g_bk7258_wifi);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_wifi_lower_register(&g_bk7258_wifi);
}
