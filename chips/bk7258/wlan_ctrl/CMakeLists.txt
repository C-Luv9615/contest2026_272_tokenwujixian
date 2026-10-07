# Cross-core Wi-Fi control plane (see wlan_ctrl.h).
#
#   wlan_ctrl_server.c  CP: the request loop + the Armino operations
#   wlan_ctrl_client.c  AP: the wifi_ctrl_*() client
#   wlan_ctrl_cli.c     AP: the NSH front end for it
#
# CP owns wlan0 and the Armino STA, so the work happens there; AP only
# sends requests.  The channel is rpmsg_char ("wlan-ctrl"), which needs no
# nuttx change -- CONFIG_RPMSG_CHAR does the rest.

if(CONFIG_BK7258_COMPONENT_CP)
  target_sources(arch PRIVATE wlan_ctrl_server.c)

  # The server calls Armino's STA/scan entry points and needs
  # modules/wifi.h for their types.  set_property with TARGET_DIRECTORY
  # because the source goes into `arch`, whose directory scope is not this
  # one.  Scoped to this one file so the Armino include root does not
  # shadow headers for the rest of arch.  The path is computed here rather
  # than taken from wifi_armino's BK7258_ARMINO_BASELINE_DIR: that variable
  # is set inside that subdirectory and is not visible from here.
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/wlan_ctrl_server.c"
               TARGET_DIRECTORY arch
               APPEND PROPERTY COMPILE_OPTIONS
               "-I${CMAKE_CURRENT_SOURCE_DIR}/../armino_baseline/cp/include")
endif()

if(CONFIG_BK7258_COMPONENT_AP)
  target_sources(arch PRIVATE wlan_ctrl_client.c)

  # NSH front end.  wifi_ctrl_client.c is also meant to be compiled into
  # the Wi-Fi UI and ai_agent when they need it; the command is just a
  # caller.  The source with main() has to be listed first: nuttx_add_
  # application puts the -Dmain=<name>_main alias on SRCS[0] only.
  nuttx_add_application(
    NAME wifi_ctrl
    SRCS wlan_ctrl_cli.c wlan_ctrl_client.c
    STACKSIZE 4096)
endif()
