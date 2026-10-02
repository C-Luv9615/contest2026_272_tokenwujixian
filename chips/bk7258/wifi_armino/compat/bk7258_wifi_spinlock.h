/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_wifi_spinlock.h
 *
 * Replaces Armino cp/components/bk_wifi/include/wifi_spinlock.h for phase
 * one (docs/compose/spec/bk7258-wifi-native.md, S2.7).
 *
 * skbuff.h includes "wifi_spinlock.h" from its own directory, so no include
 * path can redirect it. This file is force-included (-include) into the
 * components that reach it and defines its guard first, so the Armino copy
 * compiles to nothing; no Armino source is modified.
 *
 * spinlock_t is an OS type and comes from NuttX. The lock operations stay
 * no-ops, as they are in the baseline: the Armino Wi-Fi host path was
 * written against these empty macros, and making them real locks would
 * change its behaviour. The NuttX calls they correspond to are noted next to
 * each one for when the path is moved onto NuttX locking.
 ****************************************************************************/

#ifndef __SPIN_LOCK_H_
#define __SPIN_LOCK_H_

/* Same include order NuttX's <stdio.h> reaches it by (via fs.h): the
 * spinlock_t variant depends on whether nuttx/compiler.h came first, so do
 * not add it here.
 */

#include <nuttx/config.h>
#include <nuttx/spinlock_type.h>

/* NuttX: spin_lock_init(l) */
#define spin_lock_init(_lock)                do { } while (0)

/* No NuttX counterpart: bottom halves do not exist on NuttX. */
#define spin_lock_bh(...)                    do { } while (0)
#define spin_unlock_bh(...)                  do { } while (0)

/* NuttX: spin_lock(l) / spin_unlock(l) */
#define spin_lock(a)                         do { (void)(a); } while (0)
#define spin_unlock(a)                       do { (void)(a); } while (0)

/* NuttX: flags = spin_lock_irqsave(l); spin_unlock_irqrestore(l, flags) */
#define spin_lock_irqsave(a, flags)          do { (void)(flags); } while (0)
#define spin_unlock_irqrestore(a, flags)     do { (void)(flags); } while (0)

#endif /* __SPIN_LOCK_H_ */
