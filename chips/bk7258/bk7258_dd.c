/****************************************************************************
 * chips/bk7258/bk7258_dd.c
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
 * Armino device-driver init table
 *
 * The two symbols of middleware/driver/common/dd.c.  g_dd_init() walks a
 * table of {device, init, exit} and calls each init; g_dd_exit() does the
 * same with the exit column.
 *
 * In this build the table is EMPTY, and that is faithful rather than an
 * oversight: the small table is selected because CONFIG_SOC_BK7236XX is
 * defined, and its only entry is gated on CONFIG_FLASH_ORIGIN_API, which the
 * baseline sdkconfig leaves undefined.  So sizeof(dd_init_tbl) is 0, the loop
 * count is 0, and both functions return immediately.  It is called from
 * driver.c:312 between drv_model_init() and bk_timer_driver_init().
 *
 * Every configuration branch of the authority is carried (the full table for
 * the non-72xx/7236/7239/7286 parts, sctrl, flash-origin, DSP, mailbox, BT,
 * FFT, IrDA, MAC-PHY-bypass, SD card), each with the header that supplies the
 * init/exit pair it names, so a configuration flip lands on a complete table
 * instead of an empty one.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include "drv_model_pub.h"

#if CONFIG_FLASH_ORIGIN_API
#include "bk_flash.h"
#endif

#if CONFIG_DSP
#include "bk_dsp.h"
#endif

#if CONFIG_MAILBOX
#include <driver/mailbox.h>
#include "bk_mailbox.h"
#endif

#if (CONFIG_BT)
#include "bk_bt.h"
#endif

#if CONFIG_FFT
#include "bk_fft.h"
#endif

#if CONFIG_SUPPORT_IRDA
#include "bk_irda.h"
#endif

#if CONFIG_MAC_PHY_BYPASS
#include "bk_mac_phy_bypass.h"
#endif

#if CONFIG_SDCARD
#include "sdcard_pub.h"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct dd_init_s
{
  dd_device_type dev;
  void (*init)(void);
  void (*exit)(void);
} DD_INIT_S;

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if (!CONFIG_SOC_BK7256XX && !CONFIG_SOC_BK7236XX && !CONFIG_SOC_BK7239XX && \
     !CONFIG_SOC_BK7286XX)
static const DD_INIT_S dd_init_tbl[] =
{
#if !CONFIG_SYSTEM_CTRL
  {DD_DEV_TYPE_SCTRL,   sctrl_init,  sctrl_exit},
#endif

#if CONFIG_FLASH
#if CONFIG_FLASH_ORIGIN_API
  {DD_DEV_TYPE_FLASH,   flash_init,  flash_exit},
#endif
#endif

#if CONFIG_DSP
  {DD_DEV_TYPE_DSP,     dsp_init,    dsp_exit},
#endif

#if CONFIG_MAILBOX
  {DD_DEV_TYPE_MAILBOX, mailbox_driver_init, mailbox_driver_deinit},
#endif

#if (CONFIG_BT)
  {DD_DEV_TYPE_BT,      bt_init,     bt_exit},
#endif

#if CONFIG_FFT
  {DD_DEV_TYPE_FFT,     fft_init,    fft_exit},
#endif

#if CONFIG_SUPPORT_IRDA
  {DD_DEV_TYPE_IRDA,    irda_init,   irda_exit},
#endif

#if CONFIG_MAC_PHY_BYPASS
  {DD_DEV_TYPE_MPB,     mpb_init,    mpb_exit},
#endif

#if CONFIG_SDCARD
  {DD_DEV_TYPE_SDCARD,  sdcard_init, sdcard_exit},
#endif

  {DD_DEV_TYPE_NONE,    NULL,        NULL}
};
#else
static const DD_INIT_S dd_init_tbl[] =
{
#if CONFIG_FLASH
#if CONFIG_FLASH_ORIGIN_API
  {DD_DEV_TYPE_FLASH,   flash_init,  flash_exit},
#endif
#endif
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void g_dd_init(void)
{
  uint32_t i;
  uint32_t tbl_count;
  const DD_INIT_S *dd_element;

  tbl_count = sizeof(dd_init_tbl) / sizeof(DD_INIT_S);
  for (i = 0; i < tbl_count; i++)
    {
      dd_element = &dd_init_tbl[i];
      if (dd_element->dev && dd_element->init)
        {
          (dd_element->init)();
        }
      else
        {
          return;
        }
    }
}

void g_dd_exit(void)
{
  uint32_t i;
  uint32_t tbl_count;
  const DD_INIT_S *dd_element;

  tbl_count = sizeof(dd_init_tbl) / sizeof(DD_INIT_S);
  for (i = 0; i < tbl_count; i++)
    {
      dd_element = &dd_init_tbl[i];
      if (dd_element->dev && dd_element->exit)
        {
          (dd_element->exit)();
        }
      else
        {
          return;
        }
    }
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
