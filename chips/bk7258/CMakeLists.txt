# BK7258 CP/AP common chip component.

set(SRCS
    bk7258_irq.c
    bk7258_allocateheap.c
    bk7258_timer.c
    bk7258_flash.c
    bk7258_ap_boot.c
    bk7258_gpio.c)

if(CONFIG_ARCH_HAVE_MULTICPU)
  list(APPEND SRCS bk7258_cpuindex.c)
endif()

if(CONFIG_SMP)
  list(APPEND SRCS bk7258_smp.c)
endif()

if(CONFIG_BK7258_MAILBOX)
  list(APPEND SRCS bk7258_mailbox.c)
endif()

if(CONFIG_BK7258_MB_IPC_RPMSG)
  if(CONFIG_BK7258_COMPONENT_CP)
   set(BK7258_MB_IPC_ROLE ${NUTTX_CHIP_ABS_DIR}/mb_ipc/authority)
    set(BK7258_MB_IPC_SOURCES
        mb_ipc/authority_cp_mailbox_channel.c
        mb_ipc/authority_cp_mb_chnl_buff.c
        mb_ipc/authority_cp_mb_ipc.c)
  else()
    set(BK7258_MB_IPC_ROLE ${NUTTX_CHIP_ABS_DIR}/mb_ipc/authority_ap)
    set(BK7258_MB_IPC_SOURCES
        mb_ipc/authority_ap_mailbox_channel.c
        mb_ipc/authority_ap_mb_chnl_buff.c
        mb_ipc/authority_ap_mb_ipc.c
        mb_ipc/bk_mailbox_rpmsg_ap_rtos.c)
  endif()

  list(APPEND SRCS
       mb_ipc/bk_mailbox_rpmsg.c
       ${BK7258_MB_IPC_SOURCES})
endif()

if(CONFIG_BK7258_GDMA)
  list(APPEND SRCS bk7258_dma.c)
endif()

if(CONFIG_BK7258_COMPONENT_AP)
  list(APPEND SRCS bk7258_ap_start.c)
else()
  list(APPEND SRCS
       bk7258_start.c
       bk7258_sysctrl.c
       bk7258_clock.c
       bk7258_lowputc.c
       bk7258_uart.c)

  if(CONFIG_BK7258_PSRAM)
    list(APPEND SRCS bk7258_psram.c)
  endif()
endif()

if(CONFIG_BK7258_WIFI)
  list(APPEND SRCS
       saradc/nuttx_port/nuttx_port.c
       saradc/middleware/soc/common/hal/adc_hal.c
       saradc/middleware/soc/bk7258/hal/adc_ll.c
       saradc/middleware/driver/saradc/adc_driver.c
       saradc/middleware/driver/saradc/adc_statis.c
       temp_detect/bk7258_temp_detect.c
       otp/authority/middleware/driver/otp/otp_driver_v1_1.c
       otp/authority/middleware/soc/common/hal/otp_hal.c
       aon_rtc/aon_rtc_authority_hal.c
       aon_rtc/aon_rtc_authority_driver.c
       aon_rtc/nuttx_port/aon_rtc_nuttx_port.c
       trng/authority/middleware/driver/trng/trng_driver.c
       trng/authority/middleware/soc/common/hal/trng_hal.c
       trng/authority/middleware/soc/bk7258/hal/trng_ll.c
       gpio/authority/middleware/soc/common/hal/gpio_hal.c
       gpio/authority/middleware/driver/bk7258/gpio_driver.c
       gpio/nuttx_port/gpio_nuttx_port.c
       pm/authority/middleware/soc/bk7258/hal/sys_hal.c
       pm/authority/middleware/soc/bk7258/hal/sys_pm_hal.c
       pm/authority/middleware/driver/pwr_clk/pwr_clk.c)
endif()

target_sources(arch PRIVATE ${SRCS})
target_include_directories(arch PRIVATE
                           ${NUTTX_CHIP_ABS_DIR}/include
                           ${NUTTX_CHIP_ABS_DIR}/aon_pmu/nuttx_port/include
                           ${NUTTX_CHIP_ABS_DIR}/saradc/nuttx_port/include
                           ${NUTTX_CHIP_ABS_DIR}/saradc/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/saradc/middleware/soc/common/soc/include
                           ${NUTTX_CHIP_ABS_DIR}/saradc/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/saradc/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/saradc/middleware/driver/saradc
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/include
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/include/driver
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/middleware/driver/otp
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/otp/authority/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/saradc/nuttx_port/include
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/include
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/middleware/driver/rtc
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/middleware/soc/common/soc/include
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/aon_rtc/authority/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/include
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/include/driver
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/middleware/driver/trng
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/middleware/soc/common/soc/include
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/trng/authority/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/include
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/include/driver
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/driver/gpio
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/driver/bk7258
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/driver/include/bk_private
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/soc/common/soc/include
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/gpio/authority/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/pm/nuttx_port/include
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/include
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/include/cmsis
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/include/soc/bk7258
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/driver/include/bk_private
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/soc/common/hal/include
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/soc/common/soc/include
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/soc/bk7258/hal
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/soc/bk7258/soc
                           ${NUTTX_CHIP_ABS_DIR}/pm/authority/middleware/driver/pwr_clk)

if(CONFIG_BK7258_MB_IPC_RPMSG)
  target_include_directories(arch PRIVATE
                             ${NUTTX_CHIP_ABS_DIR}/wifi/glue/include
                             ${NUTTX_CHIP_ABS_DIR}/mb_ipc
                             ${NUTTX_CHIP_ABS_DIR}/mb_ipc/include
                             ${BK7258_MB_IPC_ROLE}/include
                             ${BK7258_MB_IPC_ROLE}/middleware/driver/mailbox)
endif()

if(CONFIG_AUDIO_BK7258)
  add_subdirectory(audio)
endif()

add_subdirectory(wifi)
