/****************************************************************************
 * board/bk7258-devkit/src/bk7258_reset.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET

#include <stdbool.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <sys/boardctl.h>

#ifdef CONFIG_BK7258_COMPONENT_CP
#include "bk7258_reset.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the board.
 *
 *   On the CP every status takes the AON WDT chip-level reset, which lives
 *   in the chip layer (chips/bk7258/bk7258_reset_reason.c).  SYSRESETREQ is
 *   not used there -- it only resets CPU0 and leaves CPU1/RPTUN/shared
 *   device state behind, which wedges the reboot chain.
 *
 *     BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER
 *       clear the warm tag and record RESET_SOURCE_WATCHDOG so the ROM runs
 *       the full cold chain and BL2 opens the serial download window;
 *     anything else
 *       planned reboot: warm tag plus RESET_SOURCE_REBOOT.
 *
 *   The chip layer, not this file, owns the registers.  The board only
 *   decides which of the two resets the status means.
 *
 *   On the AP the RPTUN reboot notifier, which runs before this, hands the
 *   reason to the CP (bk7258_rptun_reset returns -ENOTSUP) and the CP resets
 *   the whole chip; up_systemreset() here restarts CPU1 only and is reached
 *   only if the CP does not answer within RPTUN_STATUS_TIMEOUT_MS.
 *
 ****************************************************************************/

int board_reset(int status)
{
#ifdef CONFIG_BK7258_COMPONENT_CP
  bk7258_chip_reset(
    status == (int)BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER);
#else
  (void)status;
  up_systemreset();
#endif

  return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */
