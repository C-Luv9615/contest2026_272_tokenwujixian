/****************************************************************************
 * chips/bk7258/include/overrides/driver/wdt.h
 *
 * This file replaces cp/include/driver/wdt.h on the include path once the
 * preprocessing comparison (my_docs/bk7258-wifi/tools/header_equiv.py) shows
 * the two declare the same contract.  It is not a shim: the declarations are
 * here, and the authority header is simply no longer searched first.
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

#ifndef __CHIPS_BK7258_INCLUDE_OVERRIDES_DRIVER_AON_WDT_H
#define __CHIPS_BK7258_INCLUDE_OVERRIDES_DRIVER_AON_WDT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* The authority header pulls common/bk_include.h; callers rely on the
 * transitive surface (bk_init.c needs BK_VERIFY_ID from it).
 */

#include <common/bk_include.h>
#include <driver/aon_wdt_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 *
 * Same six declarations as cp/include/driver/aon_wdt.h:42-91.
 ****************************************************************************/

uint32_t bk_aon_wdt_get_period(void);
bk_err_t bk_aon_wdt_set_period(uint32_t timeout_ms);
bk_err_t bk_aon_wdt_stop(void);
bk_err_t bk_aon_wdt_feed(void);
uint32_t bk_aon_wdt_get_feed_time(void);
bk_err_t bk_aon_wdt_set_feed_time(uint32_t feed_time);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_OVERRIDES_DRIVER_AON_WDT_H */
