#ifndef HOSTSTUB_SEMAPHORE_H
#define HOSTSTUB_SEMAPHORE_H
#include <stdint.h>
#include <nuttx/clock.h>
typedef struct { int value; } sem_t;
#define SEM_INITIALIZER(v) { (v) }
int nxsem_init(sem_t *s, int pshared, unsigned int value);
int nxsem_destroy(sem_t *s);
int nxsem_wait(sem_t *s);
int nxsem_wait_uninterruptible(sem_t *s);
int nxsem_trywait(sem_t *s);
int nxsem_tickwait_uninterruptible(sem_t *s, sclock_t ticks);
int nxsem_post(sem_t *s);
int nxsem_get_value(sem_t *s, int *value);
#endif
