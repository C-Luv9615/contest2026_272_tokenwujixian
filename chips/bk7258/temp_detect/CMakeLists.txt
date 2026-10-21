# BK7258 on-chip temperature and voltage detection.
#
# Replaces the baseline's components/temp_detect trio (temp_detect.c,
# bk_sensor.c, volt_detect.c).  The port keeps the authority's bodies -- the
# OTP calibration load at init, the SARADC sampling and conversion, the
# NaN/"try again" value store, and the SARADC sleep-vote ordering around each
# voltage sample -- on a self-rescheduling LPWORK worker instead of a Beken
# timer plus a message-queue daemon task.

target_sources(arch PRIVATE bk7258_temp_detect.c bk7258_sensor_cal.c)

# BEFORE: wifi/hal_port/include carries its own temp_detect.h that pulls the
# hal_port hal_adc_types.h; if it shadows the baseline temp_detect.h this file
# sees both ADC type families and they redefine each other.  Putting the
# baseline component directory first keeps <temp_detect.h> on the authority
# header, and nothing else in arch includes that name.
target_include_directories(arch BEFORE PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/sdmadc
                           ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/middleware/driver/include
                           ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/temp_detect
                           ${NUTTX_CHIP_ABS_DIR}/armino_baseline/cp/components/bk_wifi/include
                           ${NUTTX_CHIP_ABS_DIR}/armino_baseline/build/bk7258/iperf/bk7258/armino/partitions/_build)
