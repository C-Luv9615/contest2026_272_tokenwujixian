#ifndef HOSTSTUB_SCHED_H
#define HOSTSTUB_SCHED_H
#include <stdint.h>
typedef int32_t pid_t;
typedef struct tcb_s *FAR_TCB;
#define SCHED_PRIORITY_MIN 0
#define SCHED_PRIORITY_MAX 255
struct tcb_s { int dummy; };
pid_t nxsched_gettid(void);
struct tcb_s *nxsched_get_tcb(pid_t pid);
void nxsched_set_param(pid_t pid, const void *param);
void sched_lock(void);
void sched_unlock(void);
int sched_yield(void);
int task_delete(pid_t pid);
#define sched_idletask() 0
#endif
