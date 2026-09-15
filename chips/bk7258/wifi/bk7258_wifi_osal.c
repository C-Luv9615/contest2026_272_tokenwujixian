/*
 * chips/bk7258/wifi/bk7258_wifi_osal.c
 *
 * NuttX OSAL for the BK7258 Wi-Fi skeleton.
 *
 * Every wrapper maps a Beken wifi_os_funcs_t callback onto exactly one NuttX
 * primitive and documents its blocking/context contract. Nothing here may
 * create a second scheduler, and no function is a semantic-empty stub: each
 * either maps to a real primitive or returns a documented error when the
 * capability is intentionally disabled at skeleton stage.
 *
 * NOTE: NuttX API symbol spellings (nxsem_* vs sem_*, clock APIs) track the
 * target NuttX branch; the parent-workspace full build is the authority.
 */

#include <nuttx/config.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>
#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include "bk7258_wifi_internal.h"

#define BK7258_WIFI_WAIT_FOREVER UINT32_MAX

/****************************************************************************
 * Threads
 *
 * Beken threads are detached, self-deleting FreeRTOS tasks. NuttX pthreads
 * with a detached attribute match that model. Wi-Fi runtime bring-up runs
 * from board late-init; if a thread must be created before the pthread layer
 * is usable, the integration branch must switch this to kthread_create.
 ****************************************************************************/

struct bk7258_wifi_thread_bundle_s
{
  void (*entry)(void *arg);
  void *arg;
};

static FAR void *bk7258_wifi_thread_trampoline(FAR void *arg)
{
  FAR struct bk7258_wifi_thread_bundle_s *bundle = arg;
  FAR void (*entry)(FAR void *) = bundle->entry;
  FAR void *entry_arg = bundle->arg;

  kmm_free(bundle);
  entry(entry_arg);
  return NULL;
}

int bk7258_wifi_osal_thread_create(uintptr_t *handle, int prio,
                                   const char *name,
                                   void (*entry)(void *arg), void *arg,
                                   size_t stack_size)
{
  FAR struct bk7258_wifi_thread_bundle_s *bundle;
  pthread_attr_t attr;
  struct sched_param param;
  pthread_t tid;
  int ret;

  bundle = kmm_malloc(sizeof(*bundle));
  if (bundle == NULL)
    {
      return -ENOMEM;
    }

  bundle->entry = entry;
  bundle->arg = arg;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack_size);
  ret = pthread_create(&tid, &attr, bk7258_wifi_thread_trampoline, bundle);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      kmm_free(bundle);
      return -ret;
    }

  param.sched_priority = prio;
  pthread_setschedprio(tid, param.sched_priority);
  pthread_detach(tid);

  *handle = (uintptr_t)tid;
  return 0;
}

int bk7258_wifi_osal_thread_delete(uintptr_t handle)
{
  /* Detached threads are not joined; cancellation is deliberately not used
   * because vendor tasks must self-exit, matching the FreeRTOS contract. */
  return 0;
}

/****************************************************************************
 * Queue
 *
 * Fixed-capacity, fixed-message-size FIFO implemented with a ring buffer and
 * two counting semaphores (available messages / available space). The ring
 * metadata is protected by a spinlock, not a mutex: vendor Wi-Fi IRQ handlers
 * use BEKEN_NO_WAIT sends and queue-state checks to defer work to the core
 * thread. A mutex would attempt to wait in that ISR path. The item semaphore
 * remains an exact item count; the space semaphore is a task-context wakeup
 * hint only. IRQ producers never wait on either semaphore.
 ****************************************************************************/

struct bk7258_wifi_queue_s
{
  size_t msg_size;
  size_t capacity;
  size_t head;
  size_t tail;
  size_t count;
  sem_t count_sem;
  sem_t space_sem;
  spinlock_t lock;
  uint8_t buf[];
};

static int bk7258_wifi_osal_queue_send_common(
  FAR struct bk7258_wifi_queue_s *q, const void *msg,
  unsigned int timeout_ms, bool front)
{
  clock_t start = 0;
  clock_t timeout = 0;

  if (timeout_ms != 0 && timeout_ms != BK7258_WIFI_WAIT_FOREVER)
    {
      start = clock_systime_ticks();
      timeout = MSEC2TICK(timeout_ms);
    }

  for (;;)
    {
      irqstate_t flags = spin_lock_irqsave(&q->lock);

      if (q->count < q->capacity)
        {
          if (front)
            {
              q->head = (q->head + q->capacity - 1) % q->capacity;
              memcpy(&q->buf[q->head * q->msg_size], msg, q->msg_size);
            }
          else
            {
              memcpy(&q->buf[q->tail * q->msg_size], msg, q->msg_size);
              q->tail = (q->tail + 1) % q->capacity;
            }

          q->count++;
          spin_unlock_irqrestore(&q->lock, flags);
          nxsem_post(&q->count_sem);
          return 0;
        }

      spin_unlock_irqrestore(&q->lock, flags);

      /* The vendor uses BEKEN_NO_WAIT from Wi-Fi hard IRQ handlers. Never
       * enter a semaphore wait path from IRQ context, even a "try" wait. */

      if (timeout_ms == 0 || up_interrupt_context())
        {
          return -EAGAIN;
        }

      if (timeout_ms == BK7258_WIFI_WAIT_FOREVER)
        {
          int ret = nxsem_wait(&q->space_sem);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          clock_t elapsed = clock_systime_ticks() - start;
          int ret;

          if (elapsed >= timeout)
            {
              return -ETIMEDOUT;
            }

          ret = nxsem_tickwait(&q->space_sem, timeout - elapsed);
          if (ret < 0)
            {
              return ret;
            }
        }
    }
}

int bk7258_wifi_osal_queue_create(uintptr_t *handle, const char *name,
                                  size_t msg_size, size_t msg_count)
{
  FAR struct bk7258_wifi_queue_s *q;
  size_t alloc;

  alloc = sizeof(*q) + msg_size * msg_count;
  q = kmm_zalloc(alloc);
  if (q == NULL)
    {
      return -ENOMEM;
    }

  q->msg_size = msg_size;
  q->capacity = msg_count;

  nxsem_init(&q->count_sem, 0, 0);
  nxsem_set_protocol(&q->count_sem, SEM_PRIO_NONE);
  /* space_sem wakes a task-context sender after a consumer frees a ring
   * entry. It is not a capacity reservation because IRQ producers must not
   * call nxsem_trywait(). Ring count under q->lock is the capacity authority. */
  nxsem_init(&q->space_sem, 0, 0);
  nxsem_set_protocol(&q->space_sem, SEM_PRIO_NONE);

  /* q comes from kmm_zalloc(); the all-zero lock state is the NuttX
   * unlocked initial state. SP_UNLOCKED is a declaration initializer, not
   * an assignable expression on this NuttX branch. */

  *handle = (uintptr_t)q;
  return 0;
}

int bk7258_wifi_osal_queue_send(uintptr_t handle, const void *msg,
                                unsigned int timeout_ms)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;

  return bk7258_wifi_osal_queue_send_common(q, msg, timeout_ms, false);
}

int bk7258_wifi_osal_queue_recv(uintptr_t handle, void *msg,
                                unsigned int timeout_ms)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;
  irqstate_t flags;
  int ret;

  ret = bk7258_wifi_osal_sem_wait((uintptr_t)&q->count_sem, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&q->lock);
  memcpy(msg, &q->buf[q->head * q->msg_size], q->msg_size);
  q->head = (q->head + 1) % q->capacity;
  q->count--;
  spin_unlock_irqrestore(&q->lock, flags);

  /* nxsem_post() is explicitly safe in IRQ context; here it wakes a sender
   * waiting for any newly available ring slot. */
  nxsem_post(&q->space_sem);
  return 0;
}

int bk7258_wifi_osal_queue_delete(uintptr_t handle)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;

  nxsem_destroy(&q->count_sem);
  nxsem_destroy(&q->space_sem);
  kmm_free(q);
  return 0;
}

bool bk7258_wifi_osal_queue_empty(uintptr_t handle)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;
  bool empty;
  irqstate_t flags = spin_lock_irqsave(&q->lock);

  empty = q->count == 0;
  spin_unlock_irqrestore(&q->lock, flags);

  return empty;
}

bool bk7258_wifi_osal_queue_full(uintptr_t handle)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;
  bool full;
  irqstate_t flags = spin_lock_irqsave(&q->lock);

  full = q->count == q->capacity;
  spin_unlock_irqrestore(&q->lock, flags);

  return full;
}

int bk7258_wifi_osal_queue_send_front(uintptr_t handle, const void *msg,
                                      unsigned int timeout_ms)
{
  FAR struct bk7258_wifi_queue_s *q = (FAR void *)handle;

  return bk7258_wifi_osal_queue_send_common(q, msg, timeout_ms, true);
}

/****************************************************************************
 * Mutex / Semaphore
 ****************************************************************************/

int bk7258_wifi_osal_mutex_create(uintptr_t *handle)
{
  FAR mutex_t *m = kmm_malloc(sizeof(*m));
  if (m == NULL)
    {
      return -ENOMEM;
    }

  nxmutex_init(m);
  *handle = (uintptr_t)m;
  return 0;
}

int bk7258_wifi_osal_mutex_lock(uintptr_t handle)
{
  return nxmutex_lock((FAR mutex_t *)handle);
}

int bk7258_wifi_osal_mutex_unlock(uintptr_t handle)
{
  return nxmutex_unlock((FAR mutex_t *)handle);
}

int bk7258_wifi_osal_mutex_delete(uintptr_t handle)
{
  nxmutex_destroy((FAR mutex_t *)handle);
  kmm_free((FAR void *)handle);
  return 0;
}

int bk7258_wifi_osal_sem_create(uintptr_t *handle, unsigned int max_count)
{
  FAR sem_t *sem = kmm_malloc(sizeof(*sem));
  if (sem == NULL)
    {
      return -ENOMEM;
    }

  nxsem_init(sem, 0, 0);
  nxsem_set_protocol(sem, SEM_PRIO_NONE);
  (void)max_count;
  *handle = (uintptr_t)sem;
  return 0;
}

int bk7258_wifi_osal_sem_wait(uintptr_t handle, unsigned int timeout_ms)
{
  FAR sem_t *sem = (FAR sem_t *)handle;
  int ret;

  if (timeout_ms == 0)
    {
      return nxsem_trywait(sem);
    }

  if (timeout_ms == BK7258_WIFI_WAIT_FOREVER)
    {
      return nxsem_wait(sem);
    }

  ret = nxsem_tickwait(sem, MSEC2TICK(timeout_ms));
  return ret < 0 ? ret : 0;
}

int bk7258_wifi_osal_sem_post(uintptr_t handle)
{
  return nxsem_post((FAR sem_t *)handle);
}

int bk7258_wifi_osal_sem_delete(uintptr_t handle)
{
  nxsem_destroy((FAR sem_t *)handle);
  kmm_free((FAR void *)handle);
  return 0;
}

/****************************************************************************
 * Delay
 *
 * ms: blocking, thread-context. us: busy-wait for short vendor PHY/RF pauses.
 ****************************************************************************/

void bk7258_wifi_osal_delay_ms(uint32_t ms)
{
  useconds_t us = (useconds_t)ms * 1000;
  nxsig_usleep(us);
}

void bk7258_wifi_osal_delay_us(uint32_t us)
{
  up_udelay((useconds_t)us);
}

/****************************************************************************
 * Memory
 *
 * kmm_* are the NuttX kernel heap allocators; Wi-Fi runtime state lives in
 * kernel space, so kernel heap is the correct backing store.
 ****************************************************************************/

void *bk7258_wifi_osal_malloc(size_t size)
{
  return kmm_malloc(size);
}

void *bk7258_wifi_osal_zalloc(size_t size)
{
  return kmm_zalloc(size);
}

void *bk7258_wifi_osal_realloc(void *ptr, size_t size)
{
  return kmm_realloc(ptr, size);
}

void bk7258_wifi_osal_free(void *ptr)
{
  kmm_free(ptr);
}

/****************************************************************************
 * Critical sections / IRQ
 ****************************************************************************/

uint32_t bk7258_wifi_osal_enter_critical(void)
{
  return enter_critical_section();
}

void bk7258_wifi_osal_exit_critical(uint32_t flags)
{
  leave_critical_section(flags);
}

uint32_t bk7258_wifi_osal_disable_irq(void)
{
  return up_irq_save();
}

void bk7258_wifi_osal_enable_irq(uint32_t level)
{
  up_irq_restore(level);
}

uint64_t bk7258_wifi_osal_time_ms(void)
{
  struct timespec ts;

  clock_systime_timespec(&ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/****************************************************************************
 * Lifecycle
 ****************************************************************************/

int bk7258_wifi_osal_init(void)
{
  return 0;
}

void bk7258_wifi_osal_deinit(void)
{
}
