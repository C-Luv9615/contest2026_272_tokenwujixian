
	err = tempd_adc_get_raw_data(ADC_TEMP_SENSOR_CHANNEL);
	if (BK_OK != err)
		return err;

	*temperature = tempd_calculate_temperature();
	return BK_OK;
}

#if CONFIG_TEMP_DETECT
static temp_detect_config_t s_tempd = {0};
static beken_thread_t  s_tempd_task_handle = NULL;
static beken_queue_t s_tempd_msg_queue = NULL;

static void tempd_main(beken_thread_arg_t data);
extern void rwnx_cal_do_temp_detect(uint16_t cur_val, uint16_t threshold, uint16_t *last);

int temp_detect_send_msg(uint32_t msg_type)
{
	bk_err_t ret = BK_FAIL;
	tempd_msg_t msg;

	TEMPD_LOGV("send msg(%d)\n", msg_type);
	if (s_tempd_msg_queue) {
		msg.temp_msg = msg_type;

		ret = rtos_push_to_queue(&s_tempd_msg_queue, &msg, BEKEN_NO_WAIT);
		if (kNoErr != ret)
			TEMPD_LOGE("send msg failed ret:%x,m_t:%x\r\n", ret, msg_type);
	}

	return (int)ret;
}

int temp_detect_init(uint32_t init_temperature)
{
	int ret;

	TEMPD_LOGD("init temperature %d\r\n", init_temperature);
	BK_RETURN_ON_ERR(tempd_init_temperature_raw_data());

	if ((!s_tempd_task_handle) && (!s_tempd_msg_queue)) {

		ret = rtos_init_queue(&s_tempd_msg_queue,
				"tempd_q",
				sizeof(tempd_msg_t),
				TEMPD_QUEUE_LEN);
		if (BK_OK != ret) {
			TEMPD_LOGE("ceate Q failed(%d)\r\n", ret);
			return ret;
		}

		ret = rtos_create_thread(&s_tempd_task_handle,
			TEMPD_TASK_PRIO,
			"tempd",
			(beken_thread_function_t)tempd_main,
			TEMPD_TASK_STACK_SIZE,
			(beken_thread_arg_t)init_temperature);
		if (BK_OK != ret) {
			rtos_deinit_queue(&s_tempd_msg_queue);
			s_tempd_msg_queue = NULL;
			TEMPD_LOGE("create task failed(%d)\r\n", ret);
			return ret;
		}
	}

	return BK_OK;
}

//TODO by frank
int temp_detect_deinit(void)
{
	if ((s_tempd_task_handle) && (s_tempd_msg_queue)) {
		//temp_detect_send_msg(TMPD_DEINIT);

		// wait untill task exit
		//while(s_tempd_task_handle)
		//    rtos_delay_milliseconds(100);

		// set reg mod & pa to initial value, this must be happened in
		// txevm or rxsens to calibration txpwr or rxsens
		manual_cal_temp_pwr_unint();
	}

	return 0;
}

int temp_detect_stop(void)
{
#if TEMP_DETECT_ONESHOT_TIMER
    if (s_tempd.detect_oneshot_timer.function
        && rtos_is_oneshot_timer_running(&s_tempd.detect_oneshot_timer))
        temp_detect_send_msg(TMPD_PAUSE_TIMER);
#else
    if (s_tempd.detect_timer.function
		&& rtos_is_timer_running(&s_tempd.detect_timer))
		temp_detect_send_msg(TMPD_PAUSE_TIMER);
#endif
	return BK_OK;
}

int temp_detect_start(void)
{
#if TEMP_DETECT_ONESHOT_TIMER
	if (s_tempd.detect_oneshot_timer.function &&
		!rtos_is_oneshot_timer_running(&s_tempd.detect_oneshot_timer))
		temp_detect_send_msg(TMPD_RESTART_TIMER);
#else
	if (s_tempd.detect_timer.function &&
		!rtos_is_timer_running(&s_tempd.detect_timer))
		temp_detect_send_msg(TMPD_RESTART_TIMER);
#endif
	return BK_OK;
}

bool temp_detect_is_init(void)
{
	return !!((s_tempd_task_handle) && (s_tempd_msg_queue));
}

#if TEMP_DETECT_ONESHOT_TIMER
static void temp_detect_oneshot_timer_handler(void *data1, void *data2)
{
    temp_detect_send_msg(TMPD_TIMER_EXPIRED);
	temp_detect_send_msg(TMPD_RESTART_TIMER);
}
#else
static void temp_detect_timer_handler(void *data)
{
	temp_detect_send_msg(TMPD_TIMER_EXPIRED);
}
#endif

static void tempd_notify_temperature_to_calibration(uint16_t temperature)
{
	uint16_t threshold = s_tempd.detect_threshold;

#if (CONFIG_SOC_BK7256XX) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX) || (!CONFIG_STA_PS)
	rwnx_cal_do_temp_detect(temperature, threshold, &s_tempd.last_detect_val);
#else
	//ps_switch(PS_UNALLOW, PS_EVENT_TEMP, PM_RF_BIT);
	rf_module_vote_ctrl(RF_OPEN,RF_BY_TEMP_BIT);
	rwnx_cal_do_temp_detect(temperature, threshold, &s_tempd.last_detect_val);
	//ps_switch(PS_ALLOW, PS_EVENT_TEMP, PM_RF_BIT);
	rf_module_vote_ctrl(RF_CLOSE,RF_BY_TEMP_BIT);
#endif
}

static void tempd_stop(void)
{
	int err;
    
#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_stop_oneshot_timer(&s_tempd.detect_oneshot_timer);
#else
    err = rtos_stop_timer(&s_tempd.detect_timer);
#endif

	BK_ASSERT(kNoErr == err);
	TEMPD_LOGV("stop\n");
}

static void tempd_restart(void)
{
	int err;

#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_oneshot_reload_timer(&s_tempd.detect_oneshot_timer);
#else
    err = rtos_reload_timer(&s_tempd.detect_timer);
#endif
	BK_ASSERT(kNoErr == err);
	TEMPD_LOGV("restart\n");
}

static void tempd_change_config(void)
{
	temp_detect_change_configuration(ADC_TMEP_DETECT_INTERVAL,
		s_tempd.detect_threshold, ADC_TMEP_DIST_INTIAL_VAL);
}

static bool tempd_need_change_detect_interval(uint32_t detect_cnt)
{
	uint32_t detect_change_cnt = ADC_TMEP_DETECT_INTERVAL_CHANGE/
					ADC_TMEP_DETECT_INTERVAL_INIT;

	return (detect_cnt == detect_change_cnt);
}

static void tempd_detect_temperature(void)
{
	uint16_t temperature = 0;
	int err = BK_OK;
    bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 0,0);
	s_tempd.detect_cnt++;
	tempd_stop();

	err = tempd_adc_get_temperature(&temperature);
	if (BK_OK != err) {
		TEMPD_LOGW("detect failed(%d), retry\n", err);
		tempd_restart();
        bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1,0);
		return; //TODO is that correct?
	}

	TEMPD_LOGV("cnt=%d, interval=%d, last=%d, cur=%d, thr=%d\r\n",
		s_tempd.detect_cnt, s_tempd.detect_interval, s_tempd.last_detect_val,
		temperature, s_tempd.detect_threshold);

	tempd_notify_temperature_to_calibration(temperature);

	if (tempd_need_change_detect_interval(s_tempd.detect_cnt))
		temp_detect_send_msg(TMPD_CHANGE_PARAM);
	else
		tempd_restart();

    bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1,0);
}

static void tempd_init(uint32_t init_temperature)
{
	int err;

	s_tempd.last_detect_val = (uint32_t)(init_temperature);
	s_tempd.inital_data = (uint32_t)(init_temperature) + ADC_TMEP_DIST_INTIAL_VAL;
	s_tempd.detect_threshold = ADC_TMEP_LSB_PER_10DEGREE * ADC_TMEP_10DEGREE_PER_DBPWR;
	s_tempd.detect_interval = ADC_TMEP_DETECT_INTERVAL_INIT;
	s_tempd.detect_cnt = 0;
	s_tempd.dist_inital = ADC_TMEP_DIST_INTIAL_VAL;

	s_tempd.last_xtal_val = (uint32_t)(init_temperature);
	s_tempd.xtal_threshold_val = ADC_XTAL_DIST_INTIAL_VAL;

    #if (CONFIG_SOC_BK7256XX) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX)
    s_tempd.xtal_init_val = sys_drv_analog_get_xtalh_ctune();// to do,need remove old interface after all adaption is finished
    #else
    s_tempd.xtal_init_val = sddev_control(DD_DEV_TYPE_SCTRL, CMD_SCTRL_GET_XTALH_CTUNE, NULL);
    #endif

	TEMPD_LOGD("xtal inital:%d, %d, %d\r\n", s_tempd.last_xtal_val,
			  s_tempd.xtal_threshold_val, s_tempd.xtal_init_val);

	temp_detect_send_msg(TMPD_TIMER_EXPIRED);
#if CONFIG_VOLT_DETECT
	temp_detect_send_msg(VOLT_TIMER_EXPIRED);
#endif
#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_init_oneshot_timer(&s_tempd.detect_oneshot_timer,
						  s_tempd.detect_interval * 1000,
						  temp_detect_oneshot_timer_handler,
						  (void *)0,
						  (void *)0);
    BK_ASSERT(kNoErr == err);
    err = rtos_start_oneshot_timer(&s_tempd.detect_oneshot_timer);
#else
	err = rtos_init_timer(&s_tempd.detect_timer,
					  s_tempd.detect_interval * 1000,
					  temp_detect_timer_handler,
					  (void *)0);
    BK_ASSERT(kNoErr == err);

#include "temp_detect_part3.inc"
