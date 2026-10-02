/* Forwarding header (docs/compose/spec/bk7258-wifi-native.md, T10).
 *
 * Unchanged Armino sources include <netif.h> through the bare lwip header
 * directory, which phase one drops because it also shadows NuttX's POSIX
 * headers. The baseline resolved this name to lwip's netif.h, so keep that.
 */

#include "lwip/netif.h"
