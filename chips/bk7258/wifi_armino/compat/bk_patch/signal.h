/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk_patch/signal.h
 *
 * Replaces Armino cp/components/wpa_supplicant-2.10/bk_patch/signal.h for
 * phase one (docs/compose/spec/bk7258-wifi-native.md, S2.7).
 *
 * The bk_patch copy is a bare-metal stand-in for POSIX <signal.h>: signal
 * numbers plus SIG_FUNC. On NuttX it also shadowed NuttX's own <signal.h>
 * (sigset_t missing in nuttx/spawn.h). The OS header comes from NuttX; the
 * signal numbers the kept sources use (SIGALRM, SIGHUP, SIGINT, SIGTERM) have
 * the same values there. Only the two Armino additions remain, with the
 * empty bodies of the excluded bk_patch/signal.c.
 ****************************************************************************/

#ifndef __BK7258_ARMINO_COMPAT_SIGNAL_H
#define __BK7258_ARMINO_COMPAT_SIGNAL_H

#include_next <signal.h>

typedef void (*SIG_FUNC)(int);

static inline void bk_signal(int sig_num, SIG_FUNC func)
{
  (void)sig_num;
  (void)func;
}

static inline unsigned int bk_alarm(unsigned int seconds)
{
  (void)seconds;
  return 0;
}

#endif /* __BK7258_ARMINO_COMPAT_SIGNAL_H */
