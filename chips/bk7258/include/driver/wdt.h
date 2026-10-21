/****************************************************************************
 * chips/bk7258/include/driver/wdt.h
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

#ifndef __CHIPS_BK7258_INCLUDE_DRIVER_WDT_H
#define __CHIPS_BK7258_INCLUDE_DRIVER_WDT_H

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
#include <driver/wdt_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 *
 * Same eight declarations as cp/include/driver/wdt.h:47-124, checked by
 * my_docs/bk7258-wifi/tools/header_equiv.py before this file took that path.
 ****************************************************************************/

bk_err_t bk_wdt_driver_init(void);
bk_err_t bk_wdt_driver_deinit(void);
bk_err_t bk_wdt_start(uint32_t timeout_ms);

/* The authority puts .itcm_sec_code on this declaration; keep it here. */

__attribute__((section(".itcm_sec_code"))) bk_err_t bk_wdt_stop(void);
bk_err_t bk_wdt_feed(void);
uint32_t bk_wdt_get_feed_time(void);
void bk_wdt_set_feed_time(uint32_t dw_set_time);
bool bk_wdt_is_driver_inited(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_INCLUDE_DRIVER_WDT_H */
