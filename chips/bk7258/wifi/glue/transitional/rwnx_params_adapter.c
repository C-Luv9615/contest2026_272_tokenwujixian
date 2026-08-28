/*
 * NuttX-owned RWNX capability provider.
 *
 * This preserves the defaults and WIFI_CAPA_ID update policy from Armino
 * cp/components/bk_wifi/src/rwnx_params.c without importing its wiphy/RF
 * negotiation path before the MAC runtime and packet bridge are ready.
 */

#include <nuttx/config.h>
#include <nuttx/spinlock.h>

#include <stdbool.h>
#include <stdint.h>

#include <modules/wifi_types.h>

struct bk7258_rwnx_capabilities_s
{
  bool erp_on;
  bool ht_on;
  bool vht_on;
  bool he_on;
  bool ldpc_on;
  bool stbc_on;
  bool sgi;
  bool use_2040;
  bool bfmee;
  bool rx_ampdu_on;
  bool tx_ampdu_on;
  uint8_t rx_agg_num;
  uint32_t tx_agg_num;
  uint8_t vht_mcs;
  uint8_t he_mcs;
};

static struct bk7258_rwnx_capabilities_s g_rwnx_capabilities =
{
  .erp_on = true,
  .ht_on = true,
  .vht_on = true,
  .he_on = true,
  .ldpc_on = true,
  .stbc_on = false,
  .sgi = true,
  .use_2040 = false,
  .bfmee = true,
  .rx_ampdu_on = true,
  .tx_ampdu_on = true,
  .rx_agg_num = 0,
  .tx_agg_num = 0,
  .vht_mcs = 2, /* IEEE80211_VHT_MCS_SUPPORT_0_9 */
  .he_mcs = 2,  /* IEEE80211_HE_MCS_SUPPORT_0_9 */
};

void rwnx_udpate_capability(uint32_t capability, uint32_t value)
{
  irqstate_t flags = enter_critical_section();

  switch (capability)
    {
      case WIFI_CAPA_ID_ERP_EN:
        g_rwnx_capabilities.erp_on = value != 0;
        break;

      case WIFI_CAPA_ID_HT_EN:
        g_rwnx_capabilities.ht_on = value != 0;
        break;

      case WIFI_CAPA_ID_VHT_EN:
        g_rwnx_capabilities.vht_on = value != 0;
        break;

      case WIFI_CAPA_ID_HE_EN:
        g_rwnx_capabilities.he_on = value != 0;
        break;

      case WIFI_CAPA_ID_TX_AMPDU_EN:
        g_rwnx_capabilities.tx_ampdu_on = value != 0;
        break;

      case WIFI_CAPA_ID_RX_AMPDU_EN:
        g_rwnx_capabilities.rx_ampdu_on = value != 0;
        break;

      case WIFI_CAPA_ID_TX_AMPDU_NUM:
        g_rwnx_capabilities.tx_agg_num = value;
        break;

      case WIFI_CAPA_ID_RX_AMPDU_NUM:
        g_rwnx_capabilities.rx_agg_num = (uint8_t)value;
        break;

      case WIFI_CAPA_ID_VHT_MCS:
        g_rwnx_capabilities.vht_mcs = (uint8_t)value;
        break;

      case WIFI_CAPA_ID_HE_MCS:
        g_rwnx_capabilities.he_mcs = (uint8_t)value;
        break;

      case WIFI_CAPA_ID_B40_EN:
        g_rwnx_capabilities.use_2040 = value != 0;
        break;

      case WIFI_CAPA_ID_STBC_EN:
        g_rwnx_capabilities.stbc_on = value != 0;
        break;

      case WIFI_CAPA_ID_SGI_EN:
        g_rwnx_capabilities.sgi = value != 0;
        break;

      case WIFI_CAPA_ID_LDPC_EN:
        g_rwnx_capabilities.ldpc_on = value != 0;
        break;

      case WIFI_CAPA_ID_BEAMFORMEE_EN:
        g_rwnx_capabilities.bfmee = value != 0;
        break;

      case WIFI_CAPA_ID_11B_ONLY_EN:
        if (value != 0)
          {
            g_rwnx_capabilities.erp_on = false;
            g_rwnx_capabilities.ht_on = false;
            g_rwnx_capabilities.vht_on = false;
            g_rwnx_capabilities.he_on = false;
          }
        break;

      default:
        break;
    }

  leave_critical_section(flags);
}

bool rwnx_get_tx_ampdu_capa_on(void)
{
  irqstate_t flags = enter_critical_section();
  bool enabled = g_rwnx_capabilities.tx_ampdu_on;
  leave_critical_section(flags);
  return enabled;
}

uint32_t rwnx_get_tx_ampdu_num(void)
{
  irqstate_t flags = enter_critical_section();
  uint32_t count = g_rwnx_capabilities.tx_agg_num;
  leave_critical_section(flags);
  return count;
}

bool rwnx_get_rx_ampdu_capa_on(void)
{
  irqstate_t flags = enter_critical_section();
  bool enabled = g_rwnx_capabilities.rx_ampdu_on;
  leave_critical_section(flags);
  return enabled;
}

uint8_t rwnx_get_rx_ampdu_num(void)
{
  irqstate_t flags = enter_critical_section();
  uint8_t count = g_rwnx_capabilities.rx_agg_num;
  leave_critical_section(flags);
  return count;
}
