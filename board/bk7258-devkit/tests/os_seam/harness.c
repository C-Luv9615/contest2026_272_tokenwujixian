/****************************************************************************
 * board/bk7258-devkit/tests/os_seam/harness.c
 *
 * Host-side checks for the FreeRTOS -> NuttX seam
 * (chips/bk7258/wifi_armino/compat/bk7258_freertos_nuttx.c), T9.
 *
 * The seam is compiled against the stub headers in include/ and against the
 * instrumented implementations below, so what is asserted is the real
 * conversion code, not a copy of it. Four items, the ones the sdk-source
 * branch had wrong (spec T9): queue item_size, stack depth in words,
 * critical-section nesting, and queue send from interrupt context.
 *
 * Build and run from tests/: see test_bk7258_os_seam.py.
 ****************************************************************************/

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/config.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/mqueue.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

/* FreeRTOS surface the seam exports; types are the ones the seam restates. */

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef int8_t QueueHandle_t_dummy;   /* placeholder, real handle is void * */
typedef void *QueueHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;

#define pdPASS        ((BaseType_t)1)
#define pdFAIL        ((BaseType_t)0)
#define pdFALSE       ((BaseType_t)0)
#define pdTRUE        ((BaseType_t)1)
#define errQUEUE_FULL ((BaseType_t)0)
#define portMAX_DELAY ((TickType_t)0xffffffffUL)

int main(void);

/* ------------------------------------------------------------------ */
/* Instrumented NuttX stubs                                            */
/* ------------------------------------------------------------------ */

static int g_irq_masked;         /* 1 = interrupts masked */
static int g_irq_save_calls;
static int g_irq_restore_calls;
static int g_in_isr;
static int g_kthread_stack_size; /* last stack_size kthread_create saw */
static int g_mq_send_calls;
static int g_mq_ticksend_calls;

irqstate_t up_irq_save(void)
{
  irqstate_t prev = (irqstate_t)g_irq_masked;

  g_irq_save_calls++;
  g_irq_masked = 1;
  return prev;
}

void up_irq_restore(irqstate_t flags)
{
  g_irq_restore_calls++;
  g_irq_masked = (int)flags;
}

int up_interrupt_context(void)
{
  return g_in_isr;
}

void *kmm_zalloc(size_t size)
{
  void *p = calloc(1, size);
  assert(p != NULL);
  return p;
}

void *kmm_malloc(size_t size)
{
  void *p = calloc(1, size);
  assert(p != NULL);
  return p;
}

void kmm_free(void *ptr)
{
  free(ptr);
}

uint32_t getprimask(void)
{
  return 0;
}

int task_delete(pid_t pid)
{
  (void)pid;
  return 0;
}

int nxrmutex_destroy(rmutex_t *m)
{
  (void)m;
  return 0;
}

ssize_t file_mq_tickreceive(struct file *mq, void *msg, size_t len,
                            unsigned int *prio, sclock_t ticks)
{
  (void)ticks;
  return file_mq_receive(mq, msg, len, prio);
}

struct mallinfo { int arena; int ordblks; int mxordblk; int uordblks;
                  int fordblks; };

struct mallinfo mallinfo(void)
{
  struct mallinfo mm = { 0, 0, 0, 0, 0 };
  return mm;
}

int work_queue(int qid, void *work, void *worker, void *arg,
               unsigned int delay)
{
  (void)qid;
  (void)work;
  (void)worker;
  (void)arg;
  (void)delay;
  return 0;
}

int work_cancel(int qid, void *work)
{
  (void)qid;
  (void)work;
  return 0;
}

clock_t clock_systime_ticks(void)
{
  return 0;
}

int nxsig_usleep(unsigned int usec)
{
  (void)usec;
  return 0;
}

int kthread_create(const char *name, int priority, int stack_size,
                   int (*entry)(int, char **), char * const argv[])
{
  (void)name;
  (void)priority;
  (void)entry;
  (void)argv;
  g_kthread_stack_size = stack_size;
  return 1;                    /* fake pid */
}

pid_t nxsched_gettid(void)
{
  return 1;
}

struct tcb_s *nxsched_get_tcb(pid_t pid)
{
  (void)pid;
  return (struct tcb_s *)1;    /* non-NULL: "still running" */
}

void nxsched_set_param(pid_t pid, const void *param)
{
  (void)pid;
  (void)param;
}

void sched_lock(void) { }
void sched_unlock(void) { }
int sched_yield(void) { return 0; }

int nxsem_init(sem_t *s, int pshared, unsigned int value)
{
  (void)pshared;
  s->value = (int)value;
  return 0;
}

int nxsem_destroy(sem_t *s)
{
  (void)s;
  return 0;
}

int nxsem_wait(sem_t *s)
{
  s->value--;
  return 0;
}

int nxsem_wait_uninterruptible(sem_t *s)
{
  s->value--;
  return 0;
}

int nxsem_trywait(sem_t *s)
{
  if (s->value <= 0)
    {
      return -11;              /* -EAGAIN */
    }

  s->value--;
  return 0;
}

int nxsem_tickwait_uninterruptible(sem_t *s, sclock_t ticks)
{
  (void)ticks;
  s->value--;
  return 0;
}

int nxsem_post(sem_t *s)
{
  s->value++;
  return 0;
}

int nxsem_get_value(sem_t *s, int *value)
{
  *value = s->value;
  return 0;
}

int nxmutex_init(mutex_t *m) { (void)m; return 0; }
int nxmutex_lock(mutex_t *m) { (void)m; return 0; }
int nxmutex_trylock(mutex_t *m) { (void)m; return 0; }
int nxmutex_unlock(mutex_t *m) { (void)m; return 0; }
int nxmutex_destroy(mutex_t *m) { (void)m; return 0; }
int nxrmutex_init(rmutex_t *m) { (void)m; return 0; }
int nxrmutex_lock(rmutex_t *m) { (void)m; return 0; }
int nxrmutex_trylock(rmutex_t *m) { (void)m; return 0; }
int nxrmutex_ticklock(rmutex_t *m, sclock_t ticks)
{
  (void)m;
  (void)ticks;
  return 0;
}

int nxrmutex_unlock(rmutex_t *m) { (void)m; return 0; }

/* mqueue stubs: a plain ring of fixed-size items, enough to check that the
 * seam passes the caller's item_size through and copies that many bytes.
 */

int file_mq_open(struct file *mq, const char *name, int flags, int mode,
                 struct mq_attr *attr)
{
  (void)name;
  (void)flags;
  (void)mode;
  memset(mq, 0, sizeof(*mq));
  mq->attr = *attr;
  mq->depth = (int)attr->mq_maxmsg;
  mq->item_size = (int)attr->mq_msgsize;
  mq->buf = calloc((size_t)mq->depth, (size_t)mq->item_size);
  assert(mq->buf != NULL);
  return 0;
}

int file_mq_close(struct file *mq)
{
  free(mq->buf);
  mq->buf = NULL;
  return 0;
}

int file_mq_unlink(const char *name)
{
  (void)name;
  return 0;
}

static int mq_push(struct file *mq, const void *msg, size_t len)
{
  if (len != (size_t)mq->item_size)
    {
      return -22;              /* -EINVAL: length must match item_size */
    }

  if (mq->count >= mq->depth)
    {
      return -11;              /* -EAGAIN */
    }

  memcpy(mq->buf + (size_t)mq->count * (size_t)mq->item_size, msg, len);
  mq->count++;
  g_mq_send_calls++;
  return 0;
}

int file_mq_send(struct file *mq, const void *msg, size_t len, unsigned int prio)
{
  (void)prio;
  return mq_push(mq, msg, len);
}

int file_mq_ticksend(struct file *mq, const void *msg, size_t len,
                     unsigned int prio, sclock_t ticks)
{
  (void)prio;
  (void)ticks;
  g_mq_ticksend_calls++;
  return mq_push(mq, msg, len);
}

ssize_t file_mq_receive(struct file *mq, void *msg, size_t len,
                        unsigned int *prio)
{
  (void)prio;
  if (mq->count == 0)
    {
      return -11;
    }

  if (len < (size_t)mq->item_size)
    {
      return -22;
    }

  memcpy(msg, mq->buf, (size_t)mq->item_size);
  mq->count--;
  memmove(mq->buf, mq->buf + (size_t)mq->item_size,
          (size_t)mq->count * (size_t)mq->item_size);
  return (ssize_t)mq->item_size;
}

ssize_t file_mq_timedreceive(struct file *mq, void *msg, size_t len,
                             unsigned int *prio, sclock_t ticks)
{
  (void)ticks;
  return file_mq_receive(mq, msg, len, prio);
}

int file_mq_getattr(struct file *mq, struct mq_attr *attr)
{
  *attr = mq->attr;
  attr->mq_curmsgs = mq->count;
  return 0;
}

/* ------------------------------------------------------------------ */
/* The seam under test, compiled as a separate translation unit.       */
/* Declared here; defined in bk7258_freertos_nuttx.c.                  */
/* ------------------------------------------------------------------ */

QueueHandle_t xQueueGenericCreate(const UBaseType_t len,
                                  const UBaseType_t item_size,
                                  const uint8_t type);
BaseType_t xQueueGenericSend(QueueHandle_t q, const void *const item,
                             TickType_t ticks, const BaseType_t pos);
BaseType_t xQueueGenericSendFromISR(QueueHandle_t q, const void *const item,
                                    BaseType_t *const woken,
                                    const BaseType_t pos);
BaseType_t xQueueReceive(QueueHandle_t q, void *const buf, TickType_t ticks);
void vPortEnterCritical(void);
void vPortExitCritical(void);
BaseType_t xTaskCreateInSram(TaskFunction_t fn, const char *name,
                             uint16_t depth, void *arg, UBaseType_t prio,
                             TaskHandle_t *handle);

#define queueQUEUE_TYPE_BASE 0

static int g_failures;

static void check(int cond, const char *what)
{
  if (cond)
    {
      printf("ok   %s\n", what);
    }
  else
    {
      printf("FAIL %s\n", what);
      g_failures++;
    }
}

/* 1. queue item_size: the fixed-size message round-trips byte for byte. */

static void test_queue_item_size(void)
{
  unsigned char out[16];
  unsigned char in[16];
  QueueHandle_t q;
  size_t i;

  for (i = 0; i < sizeof(out); i++)
    {
      out[i] = (unsigned char)(0xa0 + i);
    }

  memset(in, 0, sizeof(in));
  q = xQueueGenericCreate(4, 16, queueQUEUE_TYPE_BASE);
  check(q != NULL, "queue item_size: created 16-byte queue");
  check(xQueueGenericSend(q, out, portMAX_DELAY, 0) == pdPASS,
        "queue item_size: send accepted");
  check(xQueueReceive(q, in, portMAX_DELAY) == pdPASS,
        "queue item_size: receive accepted");
  check(memcmp(out, in, sizeof(out)) == 0,
        "queue item_size: 16 bytes round-tripped unchanged");
}

/* 2. stack depth is in words: the seam converts to bytes for kthread. */

static void test_stack_depth_in_words(void)
{
  TaskHandle_t handle = NULL;

  g_kthread_stack_size = 0;
  check(xTaskCreateInSram(NULL, "t", 32, NULL, 0, &handle) == pdPASS,
        "stack words: task created");
  /* 32 words * 4 bytes + the 1 KiB TLS/argv allowance the seam adds. */
  check(g_kthread_stack_size == 32 * 4 + 1024,
        "stack words: 32 words became 32*4+1024 bytes");
}

/* 3. critical sections nest; only the outermost restores interrupts. */

static void test_critical_nesting(void)
{
  g_irq_save_calls = 0;
  g_irq_restore_calls = 0;
  g_irq_masked = 0;

  vPortEnterCritical();
  vPortEnterCritical();
  check(g_irq_masked == 1, "critical nesting: two enters, interrupts masked");
  vPortExitCritical();
  check(g_irq_masked == 1, "critical nesting: one exit, still masked");
  vPortExitCritical();
  check(g_irq_masked == 0, "critical nesting: outermost exit unmasks");
  check(g_irq_save_calls == 2 && g_irq_restore_calls == 1,
        "critical nesting: one restore for two levels, from the first save");
}

/* 4. a send from interrupt context never blocks and never re-masks. */

static void test_send_from_isr(void)
{
  unsigned char item[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  QueueHandle_t q;
  BaseType_t woken = pdTRUE;

  q = xQueueGenericCreate(1, 8, queueQUEUE_TYPE_BASE);
  check(q != NULL, "isr send: created depth-1 queue");

  g_in_isr = 1;
  g_irq_masked = 0;
  g_irq_save_calls = 0;
  g_mq_ticksend_calls = 0;

  check(xQueueGenericSendFromISR(q, item, &woken, 0) == pdPASS,
        "isr send: first send accepted");
  check(woken == pdFALSE, "isr send: woken flag cleared for the caller");
  check(xQueueGenericSendFromISR(q, item, &woken, 0) == errQUEUE_FULL,
        "isr send: full queue answered without blocking");
  check(g_irq_masked == 0 && g_irq_save_calls == 0,
        "isr send: no critical section taken");

  g_in_isr = 0;
}

int main(void)
{
  test_queue_item_size();
  test_stack_depth_in_words();
  test_critical_nesting();
  test_send_from_isr();

  if (g_failures != 0)
    {
      printf("%d failure(s)\n", g_failures);
      return 1;
    }

  printf("all os-seam checks passed\n");
  return 0;
}
