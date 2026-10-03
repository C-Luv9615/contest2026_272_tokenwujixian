#ifndef HOSTSTUB_KTHREAD_H
#define HOSTSTUB_KTHREAD_H
#include <stdint.h>
int kthread_create(const char *name, int priority, int stack_size,
                   int (*entry)(int, char **), char * const argv[]);
#endif
