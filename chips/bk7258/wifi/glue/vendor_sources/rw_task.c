/* Wi-Fi-only translation unit: establish Armino's global build contract. */
#include <bk_prelude.h>
#include "driver.h"
#define CONFIG_AP CONFIG_BK7258_WIFI_AP
#include "../../third_party/beken_armino/glue/bk_wifi/src/rw_task.c"
#undef CONFIG_AP
