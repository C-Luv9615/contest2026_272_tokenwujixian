/* See bk7258_scan_diag.h. */

#include <nuttx/config.h>
#include <nuttx/spinlock.h>

#include <string.h>

#include "bk7258_scan_diag.h"

static struct bk7258_scan_diag_s g_scan_diag;
static spinlock_t g_scan_diag_lock = SP_UNLOCKED;

void bk7258_scan_diag_begin(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  memset(&g_scan_diag, 0, sizeof(g_scan_diag));
  g_scan_diag.start_calls = 1;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_request_channels(uint32_t requested, uint32_t active,
                                       uint32_t passive)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  g_scan_diag.requested_channels = requested;
  g_scan_diag.active_channels = active;
  g_scan_diag.passive_channels = passive;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_record(enum bk7258_scan_diag_event_e event)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);
  uint32_t *counter = NULL;

  switch (event)
    {
      case BK7258_SCAN_DIAG_START_CALL:
        counter = &g_scan_diag.start_calls;
        break;
      case BK7258_SCAN_DIAG_START_ACCEPTED:
        counter = &g_scan_diag.start_accepted;
        break;
      case BK7258_SCAN_DIAG_START_REJECTED:
        counter = &g_scan_diag.start_rejected;
        break;
      case BK7258_SCAN_DIAG_REQUEST_BUILD_FAIL:
        counter = &g_scan_diag.request_build_fail;
        break;
      case BK7258_SCAN_DIAG_LMAC_COMPLETE:
        counter = &g_scan_diag.lmac_complete;
        break;
      case BK7258_SCAN_DIAG_LMAC_RESULT_IND:
        counter = &g_scan_diag.lmac_result_ind;
        break;
      case BK7258_SCAN_DIAG_RESULT_INSERTED:
        counter = &g_scan_diag.result_inserted;
        break;
      case BK7258_SCAN_DIAG_RESULT_TABLE_FULL:
        counter = &g_scan_diag.result_table_full;
        break;
      case BK7258_SCAN_DIAG_RESULT_COUNTRY_DROP:
        counter = &g_scan_diag.result_country_drop;
        break;
      case BK7258_SCAN_DIAG_RESULT_DUPLICATE:
        counter = &g_scan_diag.result_duplicate;
        break;
      case BK7258_SCAN_DIAG_RESULT_ALLOC_FAIL:
        counter = &g_scan_diag.result_alloc_fail;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_BEACON:
        counter = &g_scan_diag.host_mgmt_beacon;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_PROBE_RESP:
        counter = &g_scan_diag.host_mgmt_probe_resp;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_NO_STA_VIF:
        counter = &g_scan_diag.host_mgmt_no_sta_vif;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_DROP:
        counter = &g_scan_diag.host_mgmt_wpaq_drop;
        break;
      case BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_FORWARDED:
        counter = &g_scan_diag.host_mgmt_wpaq_forwarded;
        break;
      case BK7258_SCAN_DIAG_COMPLETION_CALLBACK:
        counter = &g_scan_diag.completion_callback;
        break;
      case BK7258_SCAN_DIAG_RESULT_FETCH_FAIL:
        counter = &g_scan_diag.result_fetch_fail;
        break;
    }

  if (counter != NULL)
    {
      (*counter)++;
    }

  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_result_exported(uint32_t count)
{
  irqstate_t flags = spin_lock_irqsave(&g_scan_diag_lock);

  g_scan_diag.result_exported = count;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

void bk7258_scan_diag_snapshot(struct bk7258_scan_diag_s *snapshot)
{
  irqstate_t flags;

  if (snapshot == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  memcpy(snapshot, &g_scan_diag, sizeof(*snapshot));
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}
