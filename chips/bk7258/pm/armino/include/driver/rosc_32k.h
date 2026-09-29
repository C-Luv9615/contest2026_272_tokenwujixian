/* Minimal rosc_32k.h stub for pm.c migration (2026-09-28).
 * pm.c calls bk_rosc_32k_ckest_prog (line 1718). */
#pragma once
#include <common/bk_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

void bk_rosc_32k_ckest_prog(uint32_t value);

#ifdef __cplusplus
}
#endif
