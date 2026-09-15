# BK7258 Wi-Fi skeleton component (team-owned NuttX integration).

if(CONFIG_BK7258_WIFI)

  set(BK7258_WIFI_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
  set(BK7258_ARMINO_ROOT ${BK7258_WIFI_ROOT}/third_party/beken_armino)
  set(BK7258_ARMINO_WIFI ${BK7258_ARMINO_ROOT}/glue/bk_wifi)
  set(BK7258_ARMINO_WPA ${BK7258_ARMINO_ROOT}/wpa_supplicant)
  set(BK7258_WIFI_GLUE ${BK7258_WIFI_ROOT}/glue)

  set(SRCS
      bk7258_wifi_lower.c
      bk7258_wifi_osal.c
      bk7258_wifi_packet.c
      glue/pbuf_shim.c
      bk7258_wifi_hw.c
      bk7258_wifi_adapter.c
      glue/hw_driver_shim.c
      glue/bk7258_saradc.c
      glue/system_shim.c
      glue/rtos_ext_shim.c
      glue/rtos_compat_shim.c
      glue/bk_workqueue_nuttx.c
      glue/phy_calibration_shim.c
      glue/analog_shim.c
      glue/feature_shim.c
      glue/flash_shim.c
      glue/misc_shim.c
      glue/event_shim.c
      glue/scan_diagnostics.c
      glue/platform_shim.c
      hal_port/hal_port_lpdoze.c
      hal_port/vnd_cal.c
      # hal_port_power_clk_rf.c is intentionally NOT built: the
      # authoritative bk7258 build leaves CONFIG_POWER_CLOCK_RF unset, so
      # power_clk_rf_init never runs there.  Kept on disk as the reference
      # transcription only.
      hal_port/hal_port_dvfs.c
      hal_port/hal_port_sys_int_power.c
      hal_port/hal_port_mac.c
      hal_port/funcs_fill.c
      glue/os_shim.c
      glue/net_param_shim.c
      glue/vendor_sources/rwnx_params.c
      glue/vendor_sources/rwnx_td.c
      glue/vendor_sources/rwnx_txq.c
      # VIF/STA index helpers used by rw_task.c, rwnx_tx.c and rwnx_rx.c above.
      # The wrapper existed but was never listed here, which is why rwm_mgmt_*
      # and rwm_get_monitor_vif_idx appeared as undefined symbols.
      glue/vendor_sources/rwnx_utils.c
      # Monitor callback registry + management-frame filter. Backs the
      # unconditional rwnx_rx.c (wifi_monitor_get_cb) and libwifi.a's rxl_cntrl
      # (rx_mgmt_filter), so it belongs at the same level as its consumers.
      glue/vendor_sources/lsig_monitor.c
      glue/vendor_sources/rw_ieee80211.c
      glue/vendor_sources/rwnx_tx.c
      glue/vendor_sources/rwnx_rx.c
      glue/vendor_sources/rw_task.c
      glue/vendor_sources/wifi_v2.c
      glue/vendor_sources/wifi_config.c
      glue/vendor_sources/net_param.c
      glue/vendor_sources/rw_msg_tx.c
      glue/vendor_sources/rw_msg_rx.c
      glue/vendor_sources/wifi_init.c
      glue/vendor_sources/bk_phy_adapter.c
      third_party/beken_armino/board/bk7258/vnd_cal/vnd_cal.c
      glue/vendor_sources/skbuff.c
      glue/vendor_sources/bk_rf_adapter.c
      glue/vendor_sources/bk_phy_api.c
      glue/vendor_sources/bk_phy_notify.c)
  list(APPEND SRCS glue/netif_shim.c)

  # These source-backed WPA units require the complete Armino eloop/socket
  # contract and are relevant only when runtime bring-up is explicitly
  # selected.  Keep their wrappers available without forcing that incomplete
  # contract into the default, runtime-disabled CP image.
  if(CONFIG_BK7258_WIFI_VENDOR_RUNTIME)
    set(BK7258_WPA_STA_SOURCES
        third_party/beken_armino/wpa_supplicant/src/common/ieee802_11_common.c
        third_party/beken_armino/wpa_supplicant/src/common/wpa_common.c
        third_party/beken_armino/wpa_supplicant/src/common/wpa_psk_cache.c
        third_party/beken_armino/wpa_supplicant/src/drivers/driver_beken.c
        third_party/beken_armino/wpa_supplicant/src/drivers/driver_common.c
        third_party/beken_armino/wpa_supplicant/src/drivers/drivers.c
        third_party/beken_armino/wpa_supplicant/src/l2_packet/l2_packet_none.c
        third_party/beken_armino/wpa_supplicant/src/rsn_supp/pmksa_cache.c
        third_party/beken_armino/wpa_supplicant/src/rsn_supp/wpa.c
        third_party/beken_armino/wpa_supplicant/src/rsn_supp/wpa_ie.c
        third_party/beken_armino/wpa_supplicant/src/utils/common.c
        third_party/beken_armino/wpa_supplicant/src/utils/crc32.c
        third_party/beken_armino/wpa_supplicant/src/utils/os_none.c
        third_party/beken_armino/wpa_supplicant/src/utils/wpa_debug.c
        third_party/beken_armino/wpa_supplicant/src/utils/wpabuf.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/bssid_ignore.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/bss.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/config.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/config_none.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/events.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/notify.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/op_classes.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/sme.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/wmm_ac.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/wpa_scan.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/wpa_supplicant.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/wpas_glue.c
        third_party/beken_armino/wpa_supplicant/wpa_supplicant/wnm_sta.c
        # Crypto closure, mirroring the SDK's own gating. CONFIG_USE_MBEDTLS,
        # CONFIG_PSA_MBEDTLS and CONFIG_WPA3 are all unset in this profile, so
        # the SDK selects its internal implementations instead of the
        # mbedtls/wolfssl variants. These files were already vendored but never
        # listed here - the same build-set omission class as rwnx_utils.c.
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-siv.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-unwrap.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-wrap.c
        third_party/beken_armino/wpa_supplicant/src/crypto/dh_group5.c
        third_party/beken_armino/wpa_supplicant/src/crypto/dh_groups.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha1-prf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha256-kdf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha256-prf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha384-kdf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha384-prf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha512-kdf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha512-prf.c
        third_party/beken_armino/wpa_supplicant/src/crypto/tls_none.c
        # Internal crypto branch (CONFIG_USE_MBEDTLS unset).
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-cbc.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-ctr.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-internal.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-internal-dec.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-internal-enc.c
        third_party/beken_armino/wpa_supplicant/src/crypto/aes-omac1.c
        third_party/beken_armino/wpa_supplicant/src/crypto/crypto_internal-modexp.c
        third_party/beken_armino/wpa_supplicant/src/crypto/md5.c
        third_party/beken_armino/wpa_supplicant/src/crypto/md5-internal.c
        third_party/beken_armino/wpa_supplicant/src/crypto/rc4.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha1.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha1-internal.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha1-pbkdf2.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha384.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha384-internal.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha512.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha512-internal.c
        # CONFIG_WPA3 unset, so sha256 comes from the internal set too.
        third_party/beken_armino/wpa_supplicant/src/crypto/sha256.c
        third_party/beken_armino/wpa_supplicant/src/crypto/sha256-internal.c)
    foreach(wpa_source ${BK7258_WPA_STA_SOURCES})
      string(REPLACE "/" "_" wpa_name ${wpa_source})
      set(wpa_generated ${CMAKE_CURRENT_BINARY_DIR}/bk7258_wpa/${wpa_name})
      set(BK7258_WPA_SOURCE ${BK7258_WIFI_ROOT}/${wpa_source})
      configure_file(${BK7258_WIFI_ROOT}/glue/vendor_sources/wpa_source_wrapper.c.in
                     ${wpa_generated} @ONLY)
      list(APPEND SRCS ${wpa_generated})
    endforeach()
    list(APPEND SRCS
         glue/vendor_sources/fake_socket.c
         glue/vendor_sources/eloop.c
         glue/vendor_sources/wpa_signal.c
         glue/vendor_sources/sk_intf.c
         # The WPA <-> MAC ioctl bridge and its transport. Both are STA
         # providers: driver_beken.c reaches hapd_intf_ioctl() through
         # ddrv.c:ioctl_inet(), and hapd_intf_ke_rx_handle() is what actually
         # transmits the STA MLME/EAPOL frames queued by wpa_supplicant.
         # They depend on the WPA socket contract, so they stay runtime-only.
         glue/vendor_sources/ddrv.c
         glue/vendor_sources/hostapd_intf.c
         # STA association command, called by hostapd_intf.c:974
         # (wpa_send_assoc_req). Runtime-only because its only consumer is.
         glue/vendor_sources/sa_station.c
         # Team-owned WPA handler queue object + accessors. Upstream keeps these
         # in hostapd/main_none.c, but compiling that file would drag in the
         # whole hostapd provider closure; the queue itself is shared by
         # wpa_supplicant and hostapd, so we own the four primitives instead.
         glue/wpa_queue_shim.c
         glue/vendor_sources/wpa_ctrl_iface.c
         glue/vendor_sources/wpas_main_supplicant.c)
  endif()

  target_sources(arch PRIVATE ${SRCS})
  target_include_directories(arch PRIVATE
                             ${BK7258_WIFI_ROOT}
                             ${BK7258_WIFI_ROOT}/hal_port
                             ${BK7258_WIFI_GLUE}/include
                             ${BK7258_WIFI_GLUE}/include/wpa_compat
                             ${BK7258_ARMINO_ROOT}/include
                             ${BK7258_ARMINO_ROOT}/include/common
                             ${BK7258_ARMINO_ROOT}/glue/bk_phy/include
                             ${BK7258_ARMINO_WIFI}/include
                             ${BK7258_ARMINO_WIFI}/include/bk_private
                             ${BK7258_ARMINO_WIFI}/include/generated
                             ${BK7258_ARMINO_WPA}/src/common
                             ${BK7258_ARMINO_WPA}/src/ap
                             ${BK7258_ARMINO_WPA}/src/utils
                             ${BK7258_ARMINO_WPA}/src/drivers
                             ${BK7258_ARMINO_WPA}/src
                             ${BK7258_ARMINO_WPA}/wpa_supplicant
                             ${BK7258_ARMINO_WPA}/hostapd
                             ${BK7258_ARMINO_ROOT}/board/bk7258/vnd_cal/include
                             ${BK7258_ARMINO_WPA}
                             ${CMAKE_CURRENT_SOURCE_DIR}/../../../../board/bk7258-devkit/include)

  # The wrapper translation units establish the Armino/WPA configuration
  # contract locally. Do not add its macros or forced includes to `arch`, which
  # would alter every NuttX architecture translation unit.
  foreach(required_header
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_msg.h
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_mac.h
          ${BK7258_ARMINO_WIFI}/include/generated/lmac_tx.h)
    if(NOT EXISTS ${required_header})
      message(FATAL_ERROR "Missing BK7258 generated Wi-Fi header: ${required_header}")
    endif()
  endforeach()

  if(CONFIG_BK7258_WIFI_VENDOR_LINK)
    set(VENDOR_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prebuilts)
    foreach(lib libwifi.a libbk_phy.a)
      if(NOT EXISTS ${VENDOR_PREBUILT_DIR}/${lib})
        message(FATAL_ERROR "Missing pinned BK7258 vendor archive: ${VENDOR_PREBUILT_DIR}/${lib}")
      endif()
      nuttx_add_extra_library(${VENDOR_PREBUILT_DIR}/${lib})
    endforeach()
  endif()

endif()
