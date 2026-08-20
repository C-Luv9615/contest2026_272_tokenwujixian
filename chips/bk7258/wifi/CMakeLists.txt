# BK7258 Wi-Fi skeleton component (team-owned NuttX integration).

if(CONFIG_BK7258_WIFI)

  set(SRCS
      bk7258_wifi_lower.c
      bk7258_wifi_osal.c
      bk7258_wifi_packet.c
      bk7258_wifi_hw.c)

  target_sources(arch PRIVATE ${SRCS})
  target_include_directories(arch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

endif()
