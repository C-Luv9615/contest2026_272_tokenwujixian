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

/* The former g_mmstart_* latch globals were removed 2026-09-01: the
 * bk7258_hwprobe_* facility below carries the same seen/ke_state/ret/r38
 * values across four sample points instead of two, and reports them through
 * bk7258_hwprobe_report(), so nothing read those globals any more. */

/****************************************************************************
 * Hardware register snapshots around the MM bring-up handshake
 *
 * These sample absolute SoC registers (NXMAC 0x49100000, its interrupt block
 * 0x49108000, the modem clock 0x49000000 and CRM 0x49850000) at the four
 * moments the MM_RESET/MM_START handshake passes through.  MMIO addresses are
 * fixed by the SoC and do not move when code is recompiled, which is what
 * makes this class of probe worth keeping -- unlike anything derived from a
 * disassembly of library RAM, which drifts.
 *
 * The reads have to happen inside the vendored senders (rw_msg_send_reset and
 * rw_msg_send_start): the register state being measured only exists for the
 * duration of that handshake, and we never call those two functions directly.
 * So rw_msg_tx.c keeps four one-line calls -- the same shape as
 * bk7258_scan_diag_record() -- and all probe logic, storage and formatting
 * lives here.  sa_station.c used to carry its own copy of these probes around
 * the very calls that now latch internally; it is back to pristine vendor
 * source as a result.
 *
 * Latching rather than printing in place is deliberate: printing from those
 * contexts raced with other threads' console output and destroyed the
 * measurement.
 *
 * `kestate` and `ret` carry the two values only the caller's translation unit
 * can see, keeping the vendored side to a single line: ke_state_get(TASK_MM)
 * and the rw_msg_send() return code.  Pass kestate = -1 at the reset sites,
 * which have no MM task state worth reporting, and ret = 0 on the pre sites.
 *
 * Safety note: every address read here was already being read at these exact
 * four points by the probes this replaces, on hardware, without faulting --
 * the MAC and modem are powered from the pre-reset point onward in this
 * sequence.  Do not extend the site list to places where that is not known.
 ****************************************************************************/

enum bk7258_hwprobe_site_e
{
  BK7258_HWPROBE_PRE_RESET = 0,
  BK7258_HWPROBE_POST_RESET,
  BK7258_HWPROBE_PRE_START,
  BK7258_HWPROBE_POST_START,
  BK7258_HWPROBE_NSITES
};

void bk7258_hwprobe_latch(enum bk7258_hwprobe_site_e site,
                          int kestate, int ret);
void bk7258_hwprobe_report(void);

void bk7258_scan_diag_begin(void);
void bk7258_scan_diag_request_channels(uint32_t requested, uint32_t active,
                                       uint32_t passive);
void bk7258_scan_diag_record(enum bk7258_scan_diag_event_e event);
void bk7258_scan_diag_result_exported(uint32_t count);
void bk7258_scan_diag_snapshot(struct bk7258_scan_diag_s *snapshot);

#endif /* __BK7258_WIFI_GLUE_BK7258_SCAN_DIAG_H */
