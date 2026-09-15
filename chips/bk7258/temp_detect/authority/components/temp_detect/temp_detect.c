#include <common/bk_include.h>
#include "bk_arm_arch.h"
#include "bk_misc.h"
#include <os/mem.h>
#include "bk_drv_model.h"
#include "bk_sys_ctrl.h"
#include "bk_saradc.h"
#include "bk_uart.h"

#include "sys_rtos.h"
#include <os/os.h>
#include <common/bk_kernel_err.h>
#include "bk_fake_clock.h"

#include "bk_phy.h"
#include "temp_detect_pub.h"
#include "temp_detect.h"
#include "volt_detect.h"
#include "bk_ps.h"
#include "bk_wifi_private.h"
#include <components/log.h>
#if CONFIG_SDMADC_TEMP
#include <driver/sdmadc.h>
#else
#include <driver/adc.h>
#endif
#include "drv_model.h"
#include "sys_driver.h"
#if CONFIG_FLASH_ORIGIN_API
#include "flash.h"
#endif
#include <modules/pm.h>
#include <driver/rosc_32k.h>
#include <driver/aon_rtc.h>

#define CFG_USE_TEMPERATURE_DETECT                 1
#define CFG_SUPPORT_SARADC                         1
//TODO - more optimization for temp detect
//1. Keep temperature detection related work here, such as detection task, config etc.
//2. Move temp sensor and adc related work to driver, better implenented temp sensor
//3. Define public temperature API, such as bk_tempsensor_get_temperature()
//   and temp detect module depends on tempsensor!

extern void manual_cal_tmp_pwr_init(uint16_t init_temp, uint16_t init_threshold,
		uint16_t init_dist);
saradc_desc_t tmp_detect_desc;
temp_detect_config_t g_temp_detect_config;

#if CONFIG_SDMADC_TEMP
static int16_t *s_raw_temperature_data = NULL;
#else
static uint16_t *s_raw_temperature_data = NULL;
#endif

//TODO
//Both temp detect and get_temperature api share same raw buffer,
//but we don't protect!
static int tempd_init_temperature_raw_data(void)
{
	if (!s_raw_temperature_data) {
		#if CONFIG_SDMADC_TEMP
		s_raw_temperature_data = (int16_t *)os_zalloc(ADC_TEMP_BUFFER_SIZE *
					sizeof(s_raw_temperature_data[0]));
		#else
		s_raw_temperature_data = (uint16_t *)os_zalloc(ADC_TEMP_BUFFER_SIZE *
					sizeof(s_raw_temperature_data[0]));
		#endif
	}

	if (!s_raw_temperature_data) {
		TEMPD_LOGE("oom\r\n");
		return BK_ERR_NO_MEM;
	}

	return BK_OK;
}

//TODO better to put to driver
static void temp_sensor_enable(void)
{

#if (CONFIG_SOC_BK7256XX) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX)
    sys_drv_en_tempdet(1);
#else
    uint32_t param;

    param = BLK_BIT_TEMPRATURE_SENSOR;
    sddev_control(DD_DEV_TYPE_SCTRL, CMD_SCTRL_BLK_ENABLE, &param);
#endif
    
}

static void temp_sensor_disable(void)
{
#if (CONFIG_SOC_BK7256XX) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX)
    sys_drv_en_tempdet(0);
#else

    uint32_t param;
    param = BLK_BIT_TEMPRATURE_SENSOR;
    sddev_control(DD_DEV_TYPE_SCTRL, CMD_SCTRL_BLK_DISABLE, &param);
#endif
}

#if TEMPD_DISPLAY_RAW_DATA
static void tempd_show_raw_temperature_data(void)
{
	TEMPD_LOGD("raw data ");
	for (int i = 0; i < ADC_TEMP_BUFFER_SIZE; i++) {
		BK_LOG_RAW("%04x ", s_raw_temperature_data[i]);
	}
	BK_LOG_RAW("\n");
}
#else
#define tempd_show_raw_temperature_data()
#endif

static uint16_t tempd_calculate_temperature(void)
{
	tempd_show_raw_temperature_data();

#if (CONFIG_SOC_BK7231N) || (CONFIG_SOC_BK7236A) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX)
	uint32_t sum = 0, index, count = 0;

	for (index = 5; index < ADC_TEMP_BUFFER_SIZE; index++) {
		/* 0 is invalid, but saradc may return 0 in power save mode */
		if ((0 != s_raw_temperature_data[index]) && (2048 != s_raw_temperature_data[index])) {
			#if CONFIG_SDMADC_TEMP
			sum += s_raw_temperature_data[index] + CFG_SDMADC_OFFSET; //offset half of 2^16 to be positive
			#else
			sum += s_raw_temperature_data[index];
			#endif
			count++;
		}
	}

	if (count == 0)
		s_raw_temperature_data[0] = 0;
	else {
		sum = sum / count;
		sum = sum / 4;
		s_raw_temperature_data[0] = sum;
	}
#elif (CONFIG_SOC_BK7256XX)
	uint32_t sum = 0, sum1, sum2;

	sum1 = s_raw_temperature_data[1] + s_raw_temperature_data[2];
	sum2 = s_raw_temperature_data[3] + s_raw_temperature_data[4];
	sum = sum1 / 2 + sum2 / 2;
	sum = sum / 2;
	sum = sum / 16;
	s_raw_temperature_data[0] = sum;

#else
	uint32_t sum = 0, sum1, sum2;

	sum1 = s_raw_temperature_data[1] + s_raw_temperature_data[2];
	sum2 = s_raw_temperature_data[3] + s_raw_temperature_data[4];
	sum = sum1 / 2 + sum2 / 2;
	sum = sum / 2;
	sum = sum / 4;
	s_raw_temperature_data[0] = sum;
#endif

	return s_raw_temperature_data[0];
}

#if CONFIG_SDMADC_TEMP
static bk_err_t tempd_adc_get_raw_data(sdmadc_chan_t adc_chan)
{
	sdmadc_config_t config = {0};

	temp_sensor_enable();

	int err = BK_OK;
	err = bk_sdmadc_driver_init();
	if (BK_OK != err)
		goto _release_adc;

	err = bk_sdmadc_init();
	if (BK_OK != err)
		goto _release_adc;

	config.samp_mode = 0x0;
	config.samp_numb = 0x0;
	config.samp_chan = adc_chan;
	config.comp_bpss = 0x1;
	config.cic2_bpss = 0x1;
	config.cic2_gain = 0x2d;
	config.int_enable = 0x8;
	config.cali_offset = 0x0;
	config.cali_gains  = 0x1000;

	err = bk_sdmadc_set_cfg(&config);
	if (BK_OK != err)
		goto _release_adc;

	err = bk_sdmadc_read_raw(s_raw_temperature_data, ADC_TEMP_BUFFER_SIZE);

_release_adc:
	temp_sensor_disable();
	bk_sdmadc_deinit();
	bk_sdmadc_driver_deinit();
	return err;
}
#else
static int tempd_adc_get_raw_data(adc_chan_t adc_chan)
{
	adc_config_t config = {0};
	int err = BK_OK;

	BK_RETURN_ON_ERR(bk_adc_acquire());

	temp_sensor_enable();

	err = bk_adc_init(adc_chan);
	if (BK_OK != err)
		goto _release_adc;

	config.chan = adc_chan;
	config.adc_mode = ADC_CONTINUOUS_MODE;
	config.clk = TEMP_DETEC_ADC_CLK;
	config.src_clk = ADC_SCLK_XTAL_26M;
	config.saturate_mode = ADC_TEMP_SATURATE_MODE;
	config.sample_rate = TEMP_DETEC_ADC_SAMPLE_RATE;
	config.steady_ctrl= TEMP_DETEC_ADC_STEADY_CTRL;
	config.adc_filter = 0;

	err = bk_adc_set_config(&config);
	if (BK_OK != err)
		goto _release_adc;

	err = bk_adc_enable_bypass_clalibration();
	if (BK_OK != err)
		goto _release_adc;

	err = bk_adc_start();
	if (BK_OK != err)
		goto _release_adc;

	err = bk_adc_read_raw(s_raw_temperature_data, ADC_TEMP_BUFFER_SIZE,
				ADC_READ_SEMAPHORE_WAIT_TIME);
	if (BK_OK != err) {
		err = BK_ERR_TEMPD_SAMPLE_NO_DATA;
		goto _release_adc;
	}

_release_adc:
	//TODO check it, do we need to always enable temperature sensor?
	temp_sensor_disable();
    bk_adc_stop();
	bk_adc_deinit(adc_chan);
	bk_adc_release();
	return err;
}
#endif

static int tempd_adc_get_temperature(uint16_t *temperature)
{
	int err;

#include "temp_detect_part2.inc"
