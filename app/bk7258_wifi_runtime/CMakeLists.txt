if(CONFIG_BK7258_WIFI_RUNTIME_APP)
  nuttx_add_application(
    NAME bk7258_wifi_runtime
    SRCS bk7258_wifi_runtime_main.c
    STACKSIZE 4096)
endif()
