#ifndef HOSTSTUB_MUTEX_H
#define HOSTSTUB_MUTEX_H
#include <stdint.h>
#include <nuttx/clock.h>
typedef struct { int dummy; } mutex_t;
typedef struct { int dummy; } rmutex_t;
int nxmutex_init(mutex_t *m);
int nxmutex_lock(mutex_t *m);
int nxmutex_trylock(mutex_t *m);
int nxmutex_unlock(mutex_t *m);
int nxmutex_destroy(mutex_t *m);
int nxrmutex_init(rmutex_t *m);
int nxrmutex_lock(rmutex_t *m);
int nxrmutex_trylock(rmutex_t *m);
int nxrmutex_ticklock(rmutex_t *m, sclock_t ticks);
int nxrmutex_unlock(rmutex_t *m);
int nxrmutex_destroy(rmutex_t *m);
#endif
