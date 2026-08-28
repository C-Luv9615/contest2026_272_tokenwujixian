/* NuttX has no process-signal shutdown watchdog for the vendor eloop.
 * Reuse the vendor's intentional no-op signal/alarm compatibility source. */
#include <bk_prelude.h>
#include "../../third_party/beken_armino/wpa_supplicant/bk_patch/signal.c"
