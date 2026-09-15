    err = rtos_start_timer(&s_tempd.detect_timer);
#endif
	BK_ASSERT(kNoErr == err);
}

static void tempd_deinit(void)
{
	int err;
	
#if TEMP_DETECT_ONESHOT_TIMER
    err = rtos_deinit_oneshot_timer(&s_tempd.detect_oneshot_timer);
#else
    err = rtos_deinit_timer(&s_tempd.detect_timer);
#endif
	BK_ASSERT(kNoErr == err);

	rtos_deinit_queue(&s_tempd_msg_queue);
	s_tempd_msg_queue = NULL;
	s_tempd_task_handle = NULL;
	rtos_delete_thread(NULL);
}

static void tempd_main(beken_thread_arg_t data)
{
	int err;

	tempd_init((uint32_t)data);
#if CONFIG_VOLT_DETECT
	volt_daemon_init();
#endif

	while (1) {
		tempd_msg_t msg;

		err = rtos_pop_from_queue(&s_tempd_msg_queue, &msg, BEKEN_WAIT_FOREVER);
		if (kNoErr == err) {
			switch (msg.temp_msg) {
			case TMPD_PAUSE_TIMER:
				tempd_stop();
				break;

			case TMPD_RESTART_TIMER:
				tempd_restart();
				break;

			case TMPD_CHANGE_PARAM:
				tempd_change_config();
				break;

			case TMPD_TIMER_EXPIRED:
#if CONFIG_CKMN
				bk_rosc_32k_ckest_prog(96);
#endif
				tempd_detect_temperature();
				break;
#if CONFIG_VOLT_DETECT
			case VOLT_PAUSE_TIMER:
				volt_daemon_stop();
				break;

			case VOLT_RESTART_TIMER:
				volt_daemon_restart();
				break;

			case VOLT_TIMER_EXPIRED:
				volt_daemon_polling_handler();
				break;
#endif
			case TMPD_DEINIT:
				goto tempd_exit;

			default:
				break;
			}
		}
	}

tempd_exit:
	tempd_deinit();
#if CONFIG_VOLT_DETECT
	volt_daemon_deinit();
#endif
}

void temp_detect_change_configuration(uint32_t interval, uint32_t threshold, uint32_t dist)
{
	bk_err_t err;

	if (interval == 0)
		interval = ADC_TMEP_DETECT_INTERVAL;

	if (threshold == 0)
		threshold = ADC_TMEP_LSB_PER_10DEGREE * ADC_TMEP_10DEGREE_PER_DBPWR;

	if (dist == 0)
		dist = ADC_TMEP_DIST_INTIAL_VAL;

	TEMPD_LOGD("change config, interval=%d, threshold=%d, dist=%d\r\n",
		interval, threshold, dist);

	if ((s_tempd.detect_threshold != threshold) || (s_tempd.dist_inital != dist)) {
		s_tempd.detect_threshold = threshold;
		s_tempd.dist_inital = dist;

		manual_cal_tmp_pwr_init(s_tempd.inital_data, s_tempd.detect_threshold,
						s_tempd.dist_inital);
	}

	if (s_tempd.detect_interval != interval) {
		s_tempd.detect_interval = interval;
#if TEMP_DETECT_ONESHOT_TIMER
        if (s_tempd.detect_oneshot_timer.function) {
    		err = rtos_deinit_oneshot_timer(&s_tempd.detect_oneshot_timer);
    		BK_ASSERT(kNoErr == err);
    	}
        err = rtos_init_oneshot_timer(&s_tempd.detect_oneshot_timer,
                                  s_tempd.detect_interval * 1000,
                                  temp_detect_oneshot_timer_handler,
                                  (void *)0,
                                  (void *)0);
        BK_ASSERT(kNoErr == err);
        err = rtos_start_oneshot_timer(&s_tempd.detect_oneshot_timer);
#else
		if (s_tempd.detect_timer.function) {
			err = rtos_deinit_timer(&s_tempd.detect_timer);
			BK_ASSERT(kNoErr == err);
		}

		err = rtos_init_timer(&s_tempd.detect_timer,
							  s_tempd.detect_interval * 1000,
							  temp_detect_timer_handler,
							  (void *)0);
		BK_ASSERT(kNoErr == err);

		err = rtos_start_timer(&s_tempd.detect_timer);
#endif
		BK_ASSERT(kNoErr == err);
	}
}
#else
int temp_detect_init(uint32_t init_temperature)
{
    return 0;
}

int temp_detect_deinit(void)
{
    return 0;
}


bool temp_detect_is_init(void)
{
    return 1;
}

void temp_detect_change_configuration(uint32_t interval, uint32_t threshold, uint32_t dist)
{
    return;
}
#endif  // CONFIG_TEMP_DETECT

//TODO why only check the temperature range here?
//why NOT in temp_detect? Frank check it!!!
static bool tempd_is_temperature_valid(uint16_t temperature)
{
	return ((temperature > ADC_TEMP_VAL_MIN) && (temperature < ADC_TEMP_VAL_MAX));
}

int temp_detect_get_temperature(uint32_t *temperature)
{
	uint16_t cur_temperature = 0;
	int err = BK_OK;

	if (!temperature)
		return BK_ERR_NULL_PARAM;

	BK_RETURN_ON_ERR(tempd_init_temperature_raw_data());

	for (int i = 0; i < TEMPD_MAX_RETRY_NUM; i++) {
		err = tempd_adc_get_temperature(&cur_temperature);
		if (BK_OK != err) {
			TEMPD_LOGW("get temperature failed(%d), retry\n", err);
			continue;
		}

		if (tempd_is_temperature_valid(cur_temperature))
			break;
		err = BK_ERR_TRY_AGAIN;
	}

	*temperature = cur_temperature;
	TEMPD_LOGD("get temperature %d\n", cur_temperature);
	return err;
}
