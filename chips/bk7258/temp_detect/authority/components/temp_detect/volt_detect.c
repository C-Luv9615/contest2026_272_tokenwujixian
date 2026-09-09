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
#include <components/ate.h>
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

typedef struct {
	uint16_t last_detect_val;
	uint16_t detect_interval;
	uint16_t detect_threshold;
	uint16_t inital_data;
	uint32_t detect_cnt;
#if TEMP_DETECT_ONESHOT_TIMER
    beken2_timer_t detect_oneshot_timer;
#else
	beken_timer_t detect_timer;
#endif
} volt_detect_config_t;

volt_detect_config_t s_voltd;
static uint16_t *s_raw_voltage_data = NULL;

extern void rwnx_cal_do_volt_detect(UINT16 volt_adc);

static int _volt_detect_init_adc_buffer(void)
{
	if (!s_raw_voltage_data) {
		s_raw_voltage_data = (uint16_t *)os_zalloc(ADC_TEMP_BUFFER_SIZE *
					sizeof(s_raw_voltage_data[0]));

		if (!s_raw_voltage_data) {
			TEMPD_LOGE("oom\r\n");
			return BK_ERR_NO_MEM;
		}
	}

	return BK_OK;
}

#if CONFIG_SDMADC_TEMP
static int _volt_detect_get_adc_data(sdmadc_chan_t adc_chan, uint16_t *adc_buf)
{
	sdmadc_config_t config = {0};
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

	err = bk_sdmadc_read_raw((int16_t *)adc_buf, ADC_TEMP_BUFFER_SIZE);

_release_adc:
	bk_sdmadc_deinit();
	bk_sdmadc_driver_deinit();

	return err;
}
#else
static int _volt_detect_get_adc_data(adc_chan_t adc_chan, uint16_t *adc_buf)
{
	adc_config_t config = {0};
	int err = BK_OK;

	BK_RETURN_ON_ERR(bk_adc_acquire());

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

	err = bk_adc_read_raw(adc_buf, ADC_TEMP_BUFFER_SIZE,
				ADC_READ_SEMAPHORE_WAIT_TIME);
	if (BK_OK != err) {
		err = BK_ERR_TEMPD_SAMPLE_NO_DATA;
		goto _release_adc;
	}

_release_adc:
    bk_adc_stop();
	bk_adc_deinit(adc_chan);
	bk_adc_release();
	return err;
}
#endif

static uint16_t _volt_detect_calculate_voltage(uint16_t *raw_voltage_data)
{
#if (CONFIG_SOC_BK7231N) || (CONFIG_SOC_BK7236A) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX) || (CONFIG_SOC_BK7236XX)
	uint32_t sum = 0, index, count = 0;

	for (index = 5; index < ADC_TEMP_BUFFER_SIZE; index++) {
		/* 0 is invalid, but saradc may return 0 in power save mode */
		if ((0 != raw_voltage_data[index]) && (2048 != raw_voltage_data[index])) {
			#if CONFIG_SDMADC_TEMP
			sum += (int16_t)raw_voltage_data[index] + CFG_SDMADC_OFFSET; //offset half of 2^16 to be positive
			#else
			sum += raw_voltage_data[index];
			#endif
			count++;
		}
	}

	if (count == 0)
		raw_voltage_data[0] = 0;
	else {
		sum = sum / count;
		raw_voltage_data[0] = sum;
	}
#elif (CONFIG_SOC_BK7256XX)
	uint32_t sum = 0, sum1, sum2;

	sum1 = raw_voltage_data[1] + raw_voltage_data[2];
	sum2 = raw_voltage_data[3] + raw_voltage_data[4];
	sum = sum1 / 2 + sum2 / 2;
	sum = sum / 2;
	raw_voltage_data[0] = sum;

#else
	uint32_t sum = 0, sum1, sum2;

	sum1 = raw_voltage_data[1] + raw_voltage_data[2];
	sum2 = raw_voltage_data[3] + raw_voltage_data[4];
	sum = sum1 / 2 + sum2 / 2;
	sum = sum / 2;
	raw_voltage_data[0] = sum;
#endif

	return raw_voltage_data[0];
}

#if CONFIG_VOLT_DETECT
#if TEMP_DETECT_ONESHOT_TIMER
static void _volt_detect_oneshot_timer_handler(void *data1, void *data2)
{
	int result;

    result = temp_detect_send_msg(VOLT_TIMER_EXPIRED);
    if (result != BK_OK) {
		result = temp_detect_send_msg(VOLT_RESTART_TIMER);
	    if (result != BK_OK) {
			volt_daemon_restart();
	    }
    }
}
#else
static void _volt_detect_timer_handler(void *data)
{
	temp_detect_send_msg(VOLT_TIMER_EXPIRED);
}
#endif

static void _volt_detect_notify(uint16_t volt_adc)
{
	rwnx_cal_do_volt_detect(volt_adc);

#include "volt_detect_part2.inc"
