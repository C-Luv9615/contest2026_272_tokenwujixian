/****************************************************************************
 * chips/bk7258/include/bk7258_wdt_api.h
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

#ifndef __CHIPS_BK7258_INCLUDE_BK7258_WDT_API_H
#define __CHIPS_BK7258_INCLUDE_BK7258_WDT_API_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <driver/aon_wdt_types.h>
#include <driver/wdt_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Names kept from the authority so existing callers need no change:
 * baseline units still include <driver/wdt.h>, <driver/aon_wdt.h> and the
 * private bk_wdt.h; those declarations and these must stay identical, which
 * chips/bk7258/bk7258_api_abi.c proves at compile time.
 *
 * Provenance:
 *   cp/include/driver/wdt.h            :47-124
 *   cp/include/driver/aon_wdt.h        :42-91
 *   cp/middleware/driver/include/bk_private/bk_wdt.h :23-28
 */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Interrupt watchdog (hardware, CONFIG_INT_WDT_PERIOD_MS) */

bk_err_t bk_wdt_driver_init(void);
bk_err_t bk_wdt_driver_deinit(void);
bk_err_t bk_wdt_start(uint32_t timeout_ms);
bk_err_t bk_wdt_stop(void);
bk_err_t bk_wdt_feed(void);
uint32_t bk_wdt_get_feed_time(void);
void bk_wdt_set_feed_time(uint32_t dw_set_time);
bool bk_wdt_is_driver_inited(void);

/* Task watchdog (software, CONFIG_TASK_WDT_PERIOD_MS) */

void bk_task_wdt_start(void);
void bk_task_wdt_stop(void);
void bk_task_wdt_feed(void);
void bk_task_wdt_timeout_check(void);

/* Feed path shared by both watchdogs, and the hard reset the reboot path
 * takes.  bk_wdt_feed_handle() is the TIMER_ID2 callback.
 */

void bk_int_wdt_feed(void);
void bk_wdt_feed_handle(void);
void bk_wdt_force_feed(void);
void bk_wdt_force_reboot(void);
void close_wdt(void);

/* Always-on watchdog: its period reads and writes, and its stop.  With
 * CONFIG_INT_AON_WDT unset the feed-time pair is not compiled, exactly as in
 * the authority.
 */

uint32_t bk_aon_wdt_get_period(void);
bk_err_t bk_aon_wdt_set_period(uint32_t timeout_ms);
bk_err_t bk_aon_wdt_stop(void);
bk_err_t bk_aon_wdt_feed(void);
uint32_t bk_aon_wdt_get_feed_time(void);
bk_err_t bk_aon_wdt_set_feed_time(uint32_t feed_time);
void bk_aon_wdt_feed_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_BK7258_WDT_API_H */
