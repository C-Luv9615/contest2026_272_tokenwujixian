# BK7258 reusable platform API and register readback test.
if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_BK7258_PLATFORM_TEST)
  nuttx_add_application(
    NAME bk7258_platform_test
    SRCS bk7258_platform_test_main.c
    STACKSIZE 4096)
  target_include_directories(apps_bk7258_platform_test PRIVATE
                             ${NUTTX_BOARD_ABS_DIR}/include)
endif()

if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_NETSPD)
  nuttx_add_application(
    NAME netspd
    SRCS netspd.c
    STACKSIZE 4096)
endif()

if(CONFIG_LVX_USE_DEMO_CONTEST2026_272_DRVTEST AND NOT CONFIG_BK7258_COMPONENT_AP)
  nuttx_add_application(
    NAME bk7258_drvtest
    SRCS drvtest.c
    INCLUDE_DIRECTORIES ${NUTTX_BOARD_ABS_DIR}/../../chips/bk7258/include
    STACKSIZE 4096)
endif()
