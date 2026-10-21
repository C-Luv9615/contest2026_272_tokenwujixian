/****************************************************************************
 * chips/bk7258/temp_detect/bk7258_sensor_cal.c
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
 * OTP-loaded ADC calibration
 *
 * This is the body of the baseline bk_sensor.c's bk_sensor_load_adc_cali_value()
 * moved into its own translation unit.
 *
 * Why its own TU: the loader needs the baseline's bk_saradc.h (SARADC_MODE,
 * saradc_set_calibrate_val) and sdmadc_driver.h, while bk7258_temp_detect.c
 * samples through the hal_port ADC headers.  Both families declare
 * driver/hal/hal_adc_types.h with different contents, so a single TU cannot
 * include both without redefinition errors.  Splitting keeps each TU on one
 * family and leaves the code identical to the authority.
 *
 * Behaviour, unchanged: read the GADC pair from OTP and program the SARADC
 * calibrate registers; then the SDMADC pair.  Blank OTP entries are logged
 * and skipped.  On this board the SDMADC pair is blank (the boot log's
 * "uncali sdmadc value" line comes from here) while the GADC pair is valid,
 * which is why the SARADC calibration still has to run.
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <stdint.h>

#include <components/log.h>

#include <driver/otp.h>

#include "bk_saradc.h"
#include "sdmadc_driver.h"

/* Exported by the baseline temp_detect.c and never read there; kept with the
 * authority type, which only this translation unit can see.
 */

saradc_desc_t tmp_detect_desc;

#if ((CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || \
     (CONFIG_SOC_BK7286XX)) && (CONFIG_OTP_V1)

bk_err_t bk7258_sensor_load_adc_cali_value(void)
{
  uint16_t values[2];
  bk_err_t result;

  result = bk_otp_apb_read(OTP_GADC_CALIBRATION, (uint8_t *)&values,
                           sizeof(values));
  if ((result != BK_OK) || (values[0] == 0) || (values[1] == 0))
    {
      BK_LOGW("sensor", "uncali saradc value:[%x %x]\r\n", values[0],
              values[1]);
      goto load_sdmadc;
    }

  BK_LOGD("sensor", "saradc low value:[%x]\r\n", values[0]);
  BK_LOGD("sensor", "saradc high value:[%x]\r\n", values[1]);
  saradc_set_calibrate_val(&values[0], SARADC_CALIBRATE_LOW);
  saradc_set_calibrate_val(&values[1], SARADC_CALIBRATE_HIGH);

load_sdmadc:
  result = bk_otp_apb_read(OTP_SDMADC_CALIBRATION, (uint8_t *)&values,
                           sizeof(values));
  if ((result != BK_OK) || (values[0] == 0) || (values[1] == 0))
    {
      BK_LOGW("sensor", "uncali sdmadc value:[%x %x]\r\n", values[0],
              values[1]);
      return BK_FAIL;
    }

  BK_LOGD("sensor", "sdmadc low value:[%x]\r\n", values[0]);
  BK_LOGD("sensor", "sdmadc high value:[%x]\r\n", values[1]);
  bk_sdmadc_set_calibrate_val(values[0], SARADC_CALIBRATE_LOW);
  bk_sdmadc_set_calibrate_val(values[1], SARADC_CALIBRATE_HIGH);

  return BK_OK;
}

#else

bk_err_t bk7258_sensor_load_adc_cali_value(void)
{
  return BK_FAIL;
}

#endif /* SOC && OTP_V1 */

#endif /* !CONFIG_BK7258_COMPONENT_AP */
