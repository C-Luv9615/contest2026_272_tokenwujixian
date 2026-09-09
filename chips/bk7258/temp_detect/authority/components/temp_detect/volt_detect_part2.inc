}

int volt_daemon_restart(void)
{
	int err;

#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_oneshot_reload_timer(&s_voltd.detect_oneshot_timer);
#else
    err = rtos_reload_timer(&s_voltd.detect_timer);
#endif
	TEMPD_LOGV("restart err=%d\n", err);

	return err;
}

int volt_daemon_stop(void)
{
	int err;

#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_stop_oneshot_timer(&s_voltd.detect_oneshot_timer);
#else
    err = rtos_stop_timer(&s_voltd.detect_timer);
#endif

	TEMPD_LOGV("stop err=%d\n", err);

	return err;
}

void volt_daemon_polling_handler(void)
{
	uint16_t volt_adc = 0;
	int result = BK_OK;

    bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 0,0);
	s_voltd.detect_cnt++;
	volt_daemon_stop();

	result = _volt_detect_init_adc_buffer();
	if (BK_OK == result) {
		result = _volt_detect_get_adc_data(ADC_VOLT_SENSER_CHANNEL, s_raw_voltage_data);
	}
	if (BK_OK != result) {
		TEMPD_LOGW("detect failed(%d), retry\n", result);
		volt_daemon_restart();
        bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1,0);
		return; //TODO is that correct?
	}

    volt_adc = _volt_detect_calculate_voltage(s_raw_voltage_data);
	TEMPD_LOGV("cnt=%d, interval=%d, last=%d, cur=%d, thr=%d\r\n",
		s_voltd.detect_cnt, s_voltd.detect_interval, s_voltd.last_detect_val,
		volt_adc, s_voltd.detect_threshold);

	_volt_detect_notify(volt_adc);

	volt_daemon_restart();

    bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_SARADC, 1,0);
}

void volt_daemon_init(void)
{
	int err;

	s_voltd.last_detect_val = 0;
	s_voltd.inital_data = 0;
	s_voltd.detect_threshold = 0;
	if (ate_is_enabled()) {
		s_voltd.detect_interval = ADC_TMEP_DETECT_INTERVAL_INIT;
	} else {
		s_voltd.detect_interval = ADC_TMEP_DETECT_INTERVAL;
	}
	s_voltd.detect_cnt = 0;

#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_init_oneshot_timer(&s_voltd.detect_oneshot_timer,
						  s_voltd.detect_interval * 1000,
						  _volt_detect_oneshot_timer_handler,
						  (void *)0,
						  (void *)0);
    BK_ASSERT(kNoErr == err);
    err = rtos_start_oneshot_timer(&s_voltd.detect_oneshot_timer);
#else
	err = rtos_init_timer(&s_voltd.detect_timer,
					  s_voltd.detect_interval * 1000,
					  _volt_detect_timer_handler,
					  (void *)0);
    BK_ASSERT(kNoErr == err);
    err = rtos_start_timer(&s_voltd.detect_timer);
#endif
	BK_ASSERT(kNoErr == err);
}

void volt_daemon_deinit(void)
{
	int err;

#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_deinit_oneshot_timer(&s_voltd.detect_oneshot_timer);
#else
    err = rtos_deinit_timer(&s_voltd.detect_timer);
#endif
	BK_ASSERT(kNoErr == err);
}

int volt_detect_start(void)
{
#if TEMP_DETECT_ONESHOT_TIMER
	if (s_voltd.detect_oneshot_timer.function &&
		!rtos_is_oneshot_timer_running(&s_voltd.detect_oneshot_timer))
		temp_detect_send_msg(VOLT_RESTART_TIMER);
#else
	if (s_voltd.detect_timer.function &&
		!rtos_is_timer_running(&s_voltd.detect_timer))
		temp_detect_send_msg(VOLT_RESTART_TIMER);
#endif
	return BK_OK;
}

int volt_detect_stop(void)
{
#if TEMP_DETECT_ONESHOT_TIMER
    if (s_voltd.detect_oneshot_timer.function
        && rtos_is_oneshot_timer_running(&s_voltd.detect_oneshot_timer))
        temp_detect_send_msg(VOLT_PAUSE_TIMER);
#else
    if (s_voltd.detect_timer.function
		&& rtos_is_timer_running(&s_voltd.detect_timer))
		temp_detect_send_msg(VOLT_PAUSE_TIMER);
#endif
	return BK_OK;
}

#endif

#if (CONFIG_SOC_BK7256XX) ||(CONFIG_SOC_BK7236A) || (CONFIG_SOC_BK7231N) || (CONFIG_SOC_BK7236XX) || (CONFIG_SOC_BK7239XX) || (CONFIG_SOC_BK7286XX) || (CONFIG_SOC_BK7286XX)
int volt_single_get_current_voltage(UINT32 *volt_value)
{
    int result;
    int retry_count = 3;

	if (!volt_value)
		return BK_ERR_NULL_PARAM;

	BK_RETURN_ON_ERR(_volt_detect_init_adc_buffer());

    *volt_value = 0;

    for (; retry_count > 0; retry_count--) {
		result = _volt_detect_get_adc_data(ADC_VOLT_SENSER_CHANNEL, s_raw_voltage_data);
		if (BK_OK != result) {
			TEMPD_LOGW("get volt_single failed(%d), retry\n", result);
            continue;
		}

        *volt_value = _volt_detect_calculate_voltage(s_raw_voltage_data);
        if ((ADC_TEMP_VAL_MIN < *volt_value)/* && (*volt_value < ADC_TEMP_VAL_MAX)*/) {
            break;
        }
        result = BK_ERR_TRY_AGAIN;
    }

    return result;
}
#endif
