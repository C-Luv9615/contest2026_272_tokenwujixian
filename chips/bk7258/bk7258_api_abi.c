/****************************************************************************
 * chips/bk7258/bk7258_api_abi.c
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

/****************************************************************************
 * Nativized-versus-authority API check
 *
 * Every unit we replace keeps its symbol names, so our public header has to
 * reproduce the authority's declarations exactly.  Rather than trusting a
 * review, this translation unit includes BOTH: a differing return type, a
 * differing parameter type, a differing struct or a differing enum value is a
 * compile error here and nowhere else.
 *
 * Why this is enough: a header contributes no code, only declarations.  Two
 * identical declarations merge silently, two different ones cannot coexist in
 * one TU -- so "it compiles" means "the contracts are identical", and it is
 * checked on every build with no extra tooling and no runtime test.
 *
 * Internal HAL types (wdt_hal_t, gpio_hw_t, ...) are deliberately not covered:
 * they are implementation detail of the unit, never part of the API the
 * baseline callers and the closed libraries see.
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

/* Watchdog and always-on watchdog faces. */

#include <driver/wdt.h>
#include <driver/aon_wdt.h>
#include "bk_wdt.h"
#include "bk7258_wdt_api.h"

/* Reset reason and reboot faces. */

#include <components/system.h>
#include "reset_reason.h"
#include "bk7258_reset_reason_api.h"

/* A translation unit of pure declarations is legal but some toolchains warn
 * about it; the typedef gives it a body without exporting a symbol.
 */

typedef int bk7258_api_abi_checked;

#endif /* !CONFIG_BK7258_COMPONENT_AP */
