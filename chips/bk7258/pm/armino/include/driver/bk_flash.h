/* Minimal bk_flash.h for pm.c migration (2026-09-28).
 * pm.c only needs bk_flash_power_saving_enter/exit (extern decls at
 * pm.c:191-192).  The full Armino bk_flash.h is at
 * cp/middleware/driver/include/bk_private/bk_flash.h but pulls in the
 * entire flash driver API surface.  Keep this stub until the flash
 * backend is imported. */
#pragma once
#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_flash_power_saving_enter(void);
bk_err_t bk_flash_power_saving_exit(void);

#ifdef __cplusplus
}
#endif
