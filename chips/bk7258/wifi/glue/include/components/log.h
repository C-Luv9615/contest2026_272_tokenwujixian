/*
 * chips/bk7258/wifi/glue/include/components/log.h
 *
 * NuttX reimplementation of the Armino BK_LOG* macros used by the vendored
 * glue. Maps onto NuttX syslog; debug/verbose are compile-time no-ops unless
 * enabled. Passphrases/PMK must never be logged through these macros.
 */

#ifndef __BK7258_WIFI_GLUE_COMPONENTS_LOG_H
#define __BK7258_WIFI_GLUE_COMPONENTS_LOG_H

#include <syslog.h>

#define BK_LOGE(tag, fmt, ...) syslog(LOG_ERR, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGW(tag, fmt, ...) syslog(LOG_WARNING, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGI(tag, fmt, ...) syslog(LOG_INFO, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGD(tag, fmt, ...) syslog(LOG_DEBUG, "[%s] " fmt, tag, ##__VA_ARGS__)
#define BK_LOGV(tag, fmt, ...) syslog(LOG_DEBUG, "[%s] " fmt, tag, ##__VA_ARGS__)

#define BK_LOG_FLUSH()

#endif /* __BK7258_WIFI_GLUE_COMPONENTS_LOG_H */
