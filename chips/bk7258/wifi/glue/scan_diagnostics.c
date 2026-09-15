/* See bk7258_scan_diag.h. */

#include <nuttx/config.h>
#include <nuttx/spinlock.h>

#include <stdint.h>
#include <string.h>
#include <syslog.h>

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

/****************************************************************************
 * Hardware register snapshots -- see the contract note in bk7258_scan_diag.h.
 *
 * Migrated here 2026-09-01 from inline probes that lived in two vendored
 * files (third_party/.../rw_msg_tx.c and sa_station.c, ~140 lines between
 * them).  Both sampled the same four moments, because sa_station's probes
 * bracketed the very calls that now latch internally, so folding them
 * together let sa_station.c go back to byte-identical vendor source.
 ****************************************************************************/

struct bk7258_hwprobe_s
{
  uint32_t r00;         /* 0x49100000  NXMAC id: reads back once out of reset */
  uint32_t r10;         /* 0x49100010  nxmac_mac_addr_low (MAC programming)   */
  uint32_t r38;         /* 0x49100038  FSM current/next state                 */
  uint32_t r54;         /* 0x49100054                                        */
  uint32_t r500;        /* 0x49100500                                        */
  uint32_t r504;        /* 0x49100504                                        */
  uint32_t r508;        /* 0x49100508                                        */
  uint32_t mdm;         /* 0x49000000  modem clock config                     */
  uint32_t crm08;       /* 0x49850008                                        */
  uint32_t crm10;       /* 0x49850010  activeclkforce (crm_mdm_reset writes)  */
  uint32_t irq_f4;      /* 0x491080f4  MAC IRQ route hal_machw_init reads     */
  uint32_t irq_7c;      /* 0x4910807c                                        */
  uint32_t irq_70;      /* 0x49108070                                        */
  int16_t  kestate;     /* ke_state_get(TASK_MM), -1 when the site has none   */
  int16_t  ret;         /* rw_msg_send() result, 0 when the site has none     */
  uint8_t  seen;
};

static struct bk7258_hwprobe_s g_hwprobe[BK7258_HWPROBE_NSITES];

static inline uint32_t bk7258_rd(uintptr_t addr)
{
  return *(volatile uint32_t *)addr;
}

void bk7258_hwprobe_latch(enum bk7258_hwprobe_site_e site,
                          int kestate, int ret)
{
  struct bk7258_hwprobe_s snap;
  irqstate_t flags;

  if ((unsigned)site >= BK7258_HWPROBE_NSITES)
    {
      return;
    }

  /* Read outside the lock: 13 MMIO loads with interrupts disabled would
   * lengthen a critical section on the bring-up path for no benefit, and
   * these are independent read-only registers. */

  snap.r00    = bk7258_rd(0x49100000);
  snap.r10    = bk7258_rd(0x49100010);
  snap.r38    = bk7258_rd(0x49100038);
  snap.r54    = bk7258_rd(0x49100054);
  snap.r500   = bk7258_rd(0x49100500);
  snap.r504   = bk7258_rd(0x49100504);
  snap.r508   = bk7258_rd(0x49100508);
  snap.mdm    = bk7258_rd(0x49000000);
  snap.crm08  = bk7258_rd(0x49850008);
  snap.crm10  = bk7258_rd(0x49850010);
  snap.irq_f4 = bk7258_rd(0x49108000 + 0xf4);
  snap.irq_7c = bk7258_rd(0x49108000 + 0x7c);
  snap.irq_70 = bk7258_rd(0x49108000 + 0x70);
  snap.kestate = (int16_t)kestate;
  snap.ret     = (int16_t)ret;
  snap.seen    = 1;

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  g_hwprobe[site] = snap;
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);
}

/* One register per line, four columns in handshake order:
 *   pre-reset  post-reset  pre-start  post-start
 *
 * Two console constraints shape this.  Width: 80 columns, truncated silently
 * -- a wide combined line has already destroyed two rounds of readings, so a
 * row is tag + 4x8 hex digits (58 chars) and nothing more.  Volume: 14 rows of
 * mostly-identical values is its own way to lose data, so a register whose
 * value never moved across the whole handshake collapses to one column.  That
 * is lossless (the four values were equal) and in practice most rows collapse,
 * leaving the ones that actually changed visible.
 */

static void bk7258_hwprobe_row(FAR const char *tag, uint32_t a, uint32_t b,
                               uint32_t c, uint32_t d)
{
  if (a == b && b == c && c == d)
    {
      syslog(LOG_INFO, "[BK7258-WIFI] %s%08lx =4\n", tag, (unsigned long)a);
      return;
    }

  syslog(LOG_INFO, "[BK7258-WIFI] %s%08lx %08lx %08lx %08lx\n",
         tag, (unsigned long)a, (unsigned long)b,
         (unsigned long)c, (unsigned long)d);
}

#define BK7258_HWPROBE_ROW(tag, field) \
  bk7258_hwprobe_row((tag), s[0].field, s[1].field, s[2].field, s[3].field)

void bk7258_hwprobe_report(void)
{
  struct bk7258_hwprobe_s s[BK7258_HWPROBE_NSITES];
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_scan_diag_lock);
  memcpy(s, g_hwprobe, sizeof(s));
  spin_unlock_irqrestore(&g_scan_diag_lock, flags);

  /* seen tells which of the four latch points were actually reached; without
   * it an all-zero row cannot be told from a row that was never sampled. */

  syslog(LOG_INFO, "[BK7258-WIFI] hp0 seen=%u%u%u%u ke=%d,%d ret=%d,%d\n",
         (unsigned)s[0].seen, (unsigned)s[1].seen,
         (unsigned)s[2].seen, (unsigned)s[3].seen,
         (int)s[2].kestate, (int)s[3].kestate,
         (int)s[1].ret, (int)s[3].ret);

  BK7258_HWPROBE_ROW("hp1 r38=", r38);
  BK7258_HWPROBE_ROW("hp2 r54=", r54);
  BK7258_HWPROBE_ROW("hp3 r00=", r00);
  BK7258_HWPROBE_ROW("hp4 r10=", r10);
  BK7258_HWPROBE_ROW("hp5 500=", r500);
  BK7258_HWPROBE_ROW("hp6 504=", r504);
  BK7258_HWPROBE_ROW("hp7 508=", r508);
  BK7258_HWPROBE_ROW("hp8 mdm=", mdm);
  BK7258_HWPROBE_ROW("hp9 c08=", crm08);
  BK7258_HWPROBE_ROW("hpa c10=", crm10);
  BK7258_HWPROBE_ROW("hpb if4=", irq_f4);
  BK7258_HWPROBE_ROW("hpc i7c=", irq_7c);
  BK7258_HWPROBE_ROW("hpd i70=", irq_70);
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
