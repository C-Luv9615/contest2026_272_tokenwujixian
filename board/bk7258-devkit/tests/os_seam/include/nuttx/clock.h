#ifndef HOSTSTUB_CLOCK_H
#define HOSTSTUB_CLOCK_H
#include <stdint.h>
#include <time.h>          /* glibc's clock_t; the seam only casts to it */
typedef long sclock_t;
#define TICK_PER_SEC 1000
#define MSEC_PER_SEC 1000
#define MSEC2TICK(ms) (ms)
#define SEC2TICK(s)   ((s) * 1000)
#define TICK2MSEC(t)  (t)
#define TICK2USEC(t)  ((t) * 1000)
clock_t clock_systime_ticks(void);
#endif
