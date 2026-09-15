/*
 * chips/bk7258/wifi/glue/include/components/shell_task.h
 *
 * Assertion output entry point. bk_wifi_adapter.c:1400 stores its address in the
 * vendor function table (`._shell_assert_out = shell_assert_out`), so libwifi.a
 * can print during a fault; upstream reaches it through components/log.h.
 *
 * Signature is upstream's (bk_cli/shell_task.c:2229). Implemented in
 * glue/system_shim.c.
 */

#ifndef __BK7258_WIFI_GLUE_COMPONENTS_SHELL_TASK_H
#define __BK7258_WIFI_GLUE_COMPONENTS_SHELL_TASK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  shell_assert_out(bool bContinue, char *format, ...);

/* Also registered into the vendor function table, so libwifi.a can raise or
 * lower its own log verbosity. Signature is upstream's
 * (include/components/shell_task.h:32). Implemented in glue/system_shim.c.
 */

void shell_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMPONENTS_SHELL_TASK_H */
