#include <bk_prelude.h>
#include <wpa_compat/main_none.h>
#define bss_iface BK7258_WIFI_WPA_STA_IFNAME
#include "../../third_party/beken_armino/wpa_supplicant/wpa_supplicant/main_supplicant.c"
#undef bss_iface
