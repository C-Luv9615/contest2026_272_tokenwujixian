/*
 * Per-scan, privacy-preserving diagnostics for the BK7258 STA scan path.
 *
 * These counters deliberately contain no frame bytes, SSIDs, BSSIDs, scan
 * IDs, credentials, or association state.  They identify the boundary where
 * a scan stops making progress: requested active channels, LMAC result
 * indications, host result parsing, or the independent host-management RX
 * route.
 */

#ifndef __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H
#define __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H

#include <stdint.h>

struct bk7258_scan_diag_s
{
  uint32_t start_calls;
  uint32_t start_accepted;
  uint32_t start_rejected;
  uint32_t request_build_fail;
  uint32_t requested_channels;
  uint32_t active_channels;
  uint32_t passive_channels;
  uint32_t lmac_complete;
  uint32_t lmac_result_ind;
  uint32_t result_inserted;
  uint32_t result_table_full;
  uint32_t result_country_drop;
  uint32_t result_duplicate;
  uint32_t result_alloc_fail;
  uint32_t host_mgmt_beacon;
  uint32_t host_mgmt_probe_resp;
  uint32_t host_mgmt_no_sta_vif;
  uint32_t host_mgmt_wpaq_drop;
  uint32_t host_mgmt_wpaq_forwarded;
  uint32_t completion_callback;
  uint32_t result_fetch_fail;
  uint32_t result_exported;
};

enum bk7258_scan_diag_event_e
{
  BK7258_SCAN_DIAG_START_CALL,
  BK7258_SCAN_DIAG_START_ACCEPTED,
  BK7258_SCAN_DIAG_START_REJECTED,
  BK7258_SCAN_DIAG_REQUEST_BUILD_FAIL,
  BK7258_SCAN_DIAG_LMAC_COMPLETE,
  BK7258_SCAN_DIAG_LMAC_RESULT_IND,
  BK7258_SCAN_DIAG_RESULT_INSERTED,
  BK7258_SCAN_DIAG_RESULT_TABLE_FULL,
  BK7258_SCAN_DIAG_RESULT_COUNTRY_DROP,
  BK7258_SCAN_DIAG_RESULT_DUPLICATE,
  BK7258_SCAN_DIAG_RESULT_ALLOC_FAIL,
  BK7258_SCAN_DIAG_HOST_MGMT_BEACON,
  BK7258_SCAN_DIAG_HOST_MGMT_PROBE_RESP,
  BK7258_SCAN_DIAG_HOST_MGMT_NO_STA_VIF,
  BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_DROP,
  BK7258_SCAN_DIAG_HOST_MGMT_WPAQ_FORWARDED,
  BK7258_SCAN_DIAG_COMPLETION_CALLBACK,
  BK7258_SCAN_DIAG_RESULT_FETCH_FAIL,
};

void bk7258_scan_diag_begin(void);
void bk7258_scan_diag_request_channels(uint32_t requested, uint32_t active,
                                       uint32_t passive);
void bk7258_scan_diag_record(enum bk7258_scan_diag_event_e event);
void bk7258_scan_diag_result_exported(uint32_t count);
void bk7258_scan_diag_snapshot(struct bk7258_scan_diag_s *snapshot);

#endif /* __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H */
