/*
 * board/bk7258-devkit/src/bk7258_wifi_board.c
 *
 * BK7258 DevKit board-specific Wi-Fi configuration.
 *
 * Skeleton status: MAC source, calibration/regulatory inputs and external
 * PA/LNA are board-level concerns per plan §11.2. Values are recorded as
 * contracts; the stable-MAC reader and country-code source are wired once the
 * board storage layout is confirmed on hardware. GPIO26=TX_EN / GPIO28=RX_EN
 * stay unmapped (EPA disabled) on this board unless an external PA/LNA is
 * actually fitted.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include "bk7258_wifi_internal.h"

int bk7258_wifi_board_init(void)
{
  return 0;
}

int bk7258_wifi_board_get_mac(uint8_t mac[6])
{
  /* Default BK7258 policy reads the base MAC from SYS_NET/Flash with an
   * OTP2/APB fallback. Skeleton returns all-zero to signal "not wired"; the
   * board must reject all-zero/all-FF and duplicate addresses. */
  mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;
  return -ENOSYS;
}
