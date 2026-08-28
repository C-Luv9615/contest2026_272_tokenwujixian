/* Wi-Fi-only translation unit: establish Armino's local build contract.
 *
 * Deliberately does NOT include bk_prelude.h, unlike every other wrapper here.
 * The prelude claims the __SPIN_LOCK_H_ guard and installs NuttX's spinlock API,
 * whose spin_lock_irqsave() takes one argument. skbuff.c uses the Linux-style
 * two-argument form (skbuff.c:31,50,71,91), which only the vendor's own
 * wifi_spinlock.h dummy macros satisfy, so pulling in the prelude breaks this
 * file with "macro passed 2 arguments, but takes just 1".
 *
 * Consequence: os_sram_zalloc() is not macro-mapped here the way the prelude
 * would do it, so glue/os_shim.c provides it as a real function instead.
 */

#include "../../third_party/beken_armino/glue/bk_wifi/src/skbuff.c"
