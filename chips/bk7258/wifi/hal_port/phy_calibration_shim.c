/*
 * DISABLED 2026-09-28: these no-op stubs overrode the real implementations
 * in libbk_phy.a (manual_cal_bk7236.c.obj).  The real manual_cal_get_cali_xtal
 * returns g_xtal (a BSS variable updated at runtime by calibration), while
 * this stub returned g_default_xtal (a compile-time constant 0x3a).  The real
 * manual_cal_get_tx_power/set_tx_power have 120/140-byte implementations that
 * perform actual TX power calibration.  Commenting out these stubs lets the
 * libbk_phy.a implementations link through.
 *
 * Original comment (kept for provenance):
 * Read-only calibration facade for vendor PHY public APIs.
 * Per-unit OTP/Flash calibration storage is not adapted at this stage.
 */

#if 0 /* DISABLED: use libbk_phy.a real implementations instead */

#include <modules/wifi.h>
#include <syslog.h>

/* Defined by the BK7258 vendor-cal board source linked in both build paths.
 * This is a PHY trim code, not an oscillator frequency in Hz. */
extern const UINT32 g_default_xtal;

int manual_cal_get_tx_power(wifi_standard standard, float *power_dbm)
{
  (void)standard;
  (void)power_dbm;
  return BK_ERR_NOT_SUPPORT;
}

int manual_cal_set_tx_power(wifi_standard standard, float power_dbm)
{
  (void)standard;
  (void)power_dbm;
  return BK_ERR_NOT_SUPPORT;
}

UINT32 manual_cal_get_cali_xtal(void)
{
  static bool reported;

  if (!reported)
    {
      reported = true;
      syslog(LOG_INFO, "[BK7258-WIFI] factory xtal trim=%lu\n",
             (unsigned long)g_default_xtal);
    }

  return g_default_xtal;
}

#endif /* DISABLED */
