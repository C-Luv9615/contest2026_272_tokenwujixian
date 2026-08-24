# BK7258 Wi-Fi skeleton component (team-owned NuttX integration).

if(CONFIG_BK7258_WIFI)

  set(SRCS
      bk7258_wifi_lower.c
      bk7258_wifi_osal.c
      bk7258_wifi_packet.c
      bk7258_wifi_hw.c
      glue/hw_driver_shim.c)

  target_sources(arch PRIVATE ${SRCS})
  target_include_directories(arch PRIVATE
                             ${CMAKE_CURRENT_SOURCE_DIR}
                             ${CMAKE_CURRENT_SOURCE_DIR}/glue/include
                             ${CMAKE_CURRENT_SOURCE_DIR}/third_party/beken_armino/include)

endif()
