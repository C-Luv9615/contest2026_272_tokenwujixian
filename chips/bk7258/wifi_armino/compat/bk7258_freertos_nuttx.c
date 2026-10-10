/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_freertos_nuttx.c
 *
 * The FreeRTOS kernel API the kept Armino sources call, on NuttX
 * (docs/compose/spec/bk7258-wifi-native.md, S2.7). The FreeRTOS kernel
 * (os_source) is excluded: one scheduler only. The symbol set is exactly
 * the 45 the link reported undefined, with the prototypes read from the
 * baseline DWARF; bk_rtos/rtos_pub.c and mem_arch.c stay unchanged above.
 *
 * Mapping:
 *  - memory   one NuttX heap (SRAM + PSRAM), allocations zeroed: FreeRTOS's
 *             heap is a .bss array, so first-use blocks read as zero and the
 *             closed library relies on it (dev hal_port/os_shim.c, team 135)
 *  - queues   NuttX mqueue; semaphores nxsem; mutexes nxrmutex
 *  - tasks    kthreads, held until vTaskStartScheduler() as on FreeRTOS
 *  - timers   HPWORK
 *  - ticks    1:1 (configTICK_RATE_HZ 1000, CONFIG_USEC_PER_TICK 1000)
 *
 * Compiled with NuttX's flags; the FreeRTOS types are restated from
 * portmacro.h (BaseType_t long, TickType_t uint32_t, stack in words).
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/mqueue.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

/* FreeRTOS constants (projdefs.h, queue.h, task.h, timers.h). */

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;

#define pdFALSE                   0
#define pdTRUE                    1
#define pdPASS                    pdTRUE
#define pdFAIL                    pdFALSE
#define errQUEUE_FULL             0
#define portMAX_DELAY             ((TickType_t)0xffffffffu)

#define queueSEND_TO_FRONT        1
#define queueOVERWRITE            2
#define queueQUEUE_TYPE_BASE      0
#define queueQUEUE_TYPE_MUTEX     1
#define queueQUEUE_TYPE_COUNTING  2
#define queueQUEUE_TYPE_BINARY    3
#define queueQUEUE_TYPE_RMUTEX    4

#define taskSCHEDULER_SUSPENDED   0
#define taskSCHEDULER_NOT_STARTED 1
#define taskSCHEDULER_RUNNING     2

#define tmrCOMMAND_START          1
#define tmrCOMMAND_RESET          2
#define tmrCOMMAND_STOP           3
#define tmrCOMMAND_CHANGE_PERIOD  4
#define tmrCOMMAND_DELETE         5
#define tmrCOMMAND_START_FROM_ISR 6
#define tmrCOMMAND_STOP_FROM_ISR  8
#define tmrCOMMAND_CHANGE_PERIOD_FROM_ISR 9

#define FREERTOS_MAX_PRIORITIES   10   /* configMAX_PRIORITIES */

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

/****************************************************************************
 * Memory
 ****************************************************************************/

void *pvPortMalloc(size_t size)
{
  return kmm_zalloc(size);
}

void vPortFree(void *p)
{
  kmm_free(p);
}

void *bk_wrap_sram_malloc(size_t size)
{
  return kmm_zalloc(size);
}

void *psram_malloc(size_t size)
{
  return kmm_zalloc(size);
}

size_t xPortGetFreeHeapSize(void)
{
  return mallinfo().fordblks;
}

void pvShowMemoryConfigInfo(void)
{
  struct mallinfo mm = mallinfo();

  syslog(LOG_INFO, "[ARMINO] heap arena=%lu used=%lu free=%lu largest=%lu\n",
         (unsigned long)mm.arena, (unsigned long)mm.uordblks,
         (unsigned long)mm.fordblks, (unsigned long)mm.mxordblk);
}

/* pm.c powers the PSRAM down when this reports no CP allocation in it
 * (pm.c:957-970). PSRAM is part of the one NuttX heap here, so it is always
 * in use.
 */

uint32_t bk_psram_heap_get_used_count(void)
{
  return 1;
}

void bk_psram_heap_get_used_state(void)
{
  pvShowMemoryConfigInfo();
}

/****************************************************************************
 * Critical sections and interrupt state
 ****************************************************************************/

static irqstate_t g_crit_flags;
static unsigned int g_crit_nesting;

/* Nested: only the outermost enter saves, only the outermost exit restores,
 * so an inner exit cannot re-enable interrupts under an outer section.
 */

void vPortEnterCritical(void)
{
  irqstate_t flags = up_irq_save();

  if (g_crit_nesting++ == 0)
    {
      g_crit_flags = flags;
    }
}

void vPortExitCritical(void)
{
  if (g_crit_nesting > 0 && --g_crit_nesting == 0)
    {
      up_irq_restore(g_crit_flags);
    }
}

int port_disable_interrupts_flag(void)
{
  return (int)up_irq_save();
}

void port_enable_interrupts_flag(int flags)
{
  up_irq_restore((irqstate_t)flags);
}

uint32_t platform_is_in_interrupt_context(void)
{
  return up_interrupt_context() ? 1 : 0;
}

/* CONFIG_ARMV8M_USEBASEPRI is off, so PRIMASK is the interrupt mask. */

uint32_t platform_local_irq_disabled(void)
{
  return (getprimask() & 1) != 0 ? 1 : 0;
}

/****************************************************************************
 * Tasks
 ****************************************************************************/

struct armino_task_s
{
  TaskFunction_t fn;
  void *arg;
};

static volatile bool g_sched_started;
static volatile unsigned int g_sched_suspended;
static sem_t g_sched_gate = SEM_INITIALIZER(0);

/* FreeRTOS priority 0..9 (rtos_pub.c has already applied
 * BK_PRIORITY_TO_NATIVE_PRIORITY) spread linearly over NuttX
 * SCHED_PRIORITY_MIN..MAX, the projection a7de5d2 validated on the board
 * (wifi/bk7258_wifi_osal.c:66-78), here fed the native value directly.
 */

static int armino_native_priority(UBaseType_t prio)
{
  const unsigned int top = FREERTOS_MAX_PRIORITIES - 1;

  if (prio > top)
    {
      prio = top;
    }

  return SCHED_PRIORITY_MIN +
         (int)((prio * (SCHED_PRIORITY_MAX - SCHED_PRIORITY_MIN) + top / 2) /
               top);
}

static int armino_task_entry(int argc, char *argv[])
{
  struct armino_task_s *task;
  TaskFunction_t fn;
  void *arg;

  if (argc < 2)
    {
      return -EINVAL;
    }

  task = (struct armino_task_s *)(uintptr_t)strtoul(argv[1], NULL, 16);
  fn = task->fn;
  arg = task->arg;
  kmm_free(task);

  /* FreeRTOS runs no task before vTaskStartScheduler(); entry_main()
   * creates its main and app threads before it.
   */

  if (!g_sched_started)
    {
      nxsem_wait_uninterruptible(&g_sched_gate);
      nxsem_post(&g_sched_gate);
    }

  fn(arg);
  return 0;
}

static BaseType_t armino_task_create(TaskFunction_t fn, const char *name,
                                     uint16_t depth, void *arg,
                                     UBaseType_t prio, TaskHandle_t *handle)
{
  struct armino_task_s *task;
  char ptr[2 + sizeof(void *) * 2 + 1];
  char *argv[2];
  int pid;

  task = kmm_malloc(sizeof(*task));
  if (task == NULL)
    {
      return pdFAIL;
    }

  task->fn = fn;
  task->arg = arg;
  snprintf(ptr, sizeof(ptr), "%lx", (unsigned long)(uintptr_t)task);
  argv[0] = ptr;
  argv[1] = NULL;

  /* depth is in StackType_t words (rtos_pub.c:128 divides bytes by 4).
   * ponytail: +1K covers NuttX's TLS and argv copy on top of the Armino
   * figure; measure with the stack monitor and trim.
   */

  pid = kthread_create(name != NULL && name[0] != '\0' ? name : "armino",
                       armino_native_priority(prio),
                       (int)depth * 4 + 1024, armino_task_entry, argv);
  if (pid < 0)
    {
      syslog(LOG_ERR, "[ARMINO] task %s create failed: %d\n",
             name != NULL ? name : "", pid);
      kmm_free(task);
      return pdFAIL;
    }

  if (handle != NULL)
    {
      *handle = (TaskHandle_t)(uintptr_t)pid;
    }

  return pdPASS;
}

BaseType_t xTaskCreateInSram(TaskFunction_t fn, const char *name,
                             uint16_t depth, void *arg, UBaseType_t prio,
                             TaskHandle_t *handle)
{
  return armino_task_create(fn, name, depth, arg, prio, handle);
}

/* PSRAM is in the same heap; no separate placement. */

BaseType_t xTaskCreateInPsram(TaskFunction_t fn, const char *name,
                              uint16_t depth, void *arg, UBaseType_t prio,
                              TaskHandle_t *handle)
{
  return armino_task_create(fn, name, depth, arg, prio, handle);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
  return (TaskHandle_t)(uintptr_t)nxsched_gettid();
}

void vTaskDelete(TaskHandle_t handle)
{
  /* task_delete(0) deletes the calling task and does not return. */

  task_delete(handle == NULL ? 0 : (pid_t)(uintptr_t)handle);
}

BaseType_t xTaskIsTaskFinished(TaskHandle_t handle)
{
  return nxsched_get_tcb((pid_t)(uintptr_t)handle) == NULL ?
         pdTRUE : pdFALSE;
}

void vTaskDelay(const TickType_t ticks)
{
  if (ticks == 0)
    {
      sched_yield();
    }
  else
    {
      nxsig_usleep(TICK2USEC(ticks));
    }
}

void vTaskPrioritySet(TaskHandle_t handle, UBaseType_t prio)
{
  struct sched_param param;

  param.sched_priority = armino_native_priority(prio);
  nxsched_set_param(handle == NULL ? 0 : (pid_t)(uintptr_t)handle, &param);
}

/* NuttX's scheduler already runs; this only releases the tasks created
 * before it, as FreeRTOS would, and returns.
 */

void vTaskStartScheduler(void)
{
  if (!g_sched_started)
    {
      g_sched_started = true;
      nxsem_post(&g_sched_gate);
    }
}

BaseType_t xTaskGetSchedulerState(void)
{
  if (!g_sched_started)
    {
      return taskSCHEDULER_NOT_STARTED;
    }

  return g_sched_suspended > 0 ? taskSCHEDULER_SUSPENDED :
                                 taskSCHEDULER_RUNNING;
}

void vTaskSuspendAll(void)
{
  sched_lock();
  g_sched_suspended++;
}

BaseType_t xTaskResumeAll(void)
{
  g_sched_suspended--;
  sched_unlock();
  return pdFALSE;
}

/****************************************************************************
 * Ticks
 ****************************************************************************/

TickType_t xTaskGetTickCount(void)
{
  return (TickType_t)clock_systime_ticks();
}

TickType_t xTaskGetTickCountFromISR(void)
{
  return (TickType_t)clock_systime_ticks();
}

/* port_tick_impl.h:14 records an AON-RTC base for rtos_get_time_diff(),
 * which lives in the excluded port and has no kept caller.
 */

void rtos_init_base_time(void)
{
}

/****************************************************************************
 * Queues, semaphores, mutexes
 ****************************************************************************/

struct armino_queue_s
{
  /* FreeRTOS's Queue_t starts with pcHead, which for a semaphore points at
   * the queue itself (queue.c:516). bk_api_ipc.c:868 passes a semaphore
   * handle where rtos_get_semaphore() expects its address, and rtos_pub.c
   * dereferences it; on FreeRTOS that yields the same handle, so the
   * baseline works by accident. Keep the same first word. Board boot
   * 2026-10-03 took an unaligned-access fault there (handle read as 0x2,
   * the type byte, so the semaphore count landed at 0x6).
   *
   * Only semaphores set it. A FreeRTOS mutex has pcHead NULL
   * (queueQUEUE_IS_MUTEX), which kmm_zalloc() already gives; a data queue
   * points it at its storage, so the same slip on a queue fails on the
   * baseline too.
   */

  void *head;
  uint8_t type;
  union
    {
      struct file mq;
      sem_t sem;
      rmutex_t mutex;
    } u;
  unsigned int id;
  UBaseType_t depth;
  UBaseType_t item_size;
  UBaseType_t max_count;
  long hiwater;
};

typedef struct armino_queue_s *QueueHandle_t;

static unsigned int g_queue_id;

static long armino_queue_count(QueueHandle_t q);

static bool armino_is_semaphore(QueueHandle_t q)
{
  return q->type == queueQUEUE_TYPE_COUNTING ||
         q->type == queueQUEUE_TYPE_BINARY;
}

static bool armino_is_mutex(QueueHandle_t q)
{
  return q->type == queueQUEUE_TYPE_MUTEX ||
         q->type == queueQUEUE_TYPE_RMUTEX;
}

static QueueHandle_t armino_sem_create(uint8_t type, UBaseType_t max,
                                       UBaseType_t initial)
{
  QueueHandle_t q = kmm_zalloc(sizeof(*q));

  if (q != NULL)
    {
      q->head = q;
      q->type = type;
      q->max_count = max;
      nxsem_init(&q->u.sem, 0, (int)initial);
    }

  return q;
}

QueueHandle_t xQueueGenericCreate(const UBaseType_t len,
                                  const UBaseType_t item_size,
                                  const uint8_t type)
{
  struct mq_attr attr;
  char name[16];
  QueueHandle_t q;
  int ret;

  /* xSemaphoreCreateBinary() is xQueueGenericCreate(1, 0, BINARY). */

  if (type == queueQUEUE_TYPE_BINARY)
    {
      return armino_sem_create(type, 1, 0);
    }

  if (item_size > CONFIG_MQ_MAXMSGSIZE || len == 0)
    {
      syslog(LOG_ERR, "[ARMINO] queue rejected: item %lu (max %d) depth %lu\n",
             (unsigned long)item_size, CONFIG_MQ_MAXMSGSIZE,
             (unsigned long)len);
      return NULL;
    }

  q = kmm_zalloc(sizeof(*q));
  if (q == NULL)
    {
      return NULL;
    }

  q->type = queueQUEUE_TYPE_BASE;
  q->id = ++g_queue_id;
  q->depth = len;
  q->item_size = item_size;

  attr.mq_maxmsg = (long)len;
  attr.mq_msgsize = (long)item_size;
  attr.mq_flags = 0;
  snprintf(name, sizeof(name), "/armq%u", q->id);
  ret = file_mq_open(&q->u.mq, name, O_RDWR | O_CREAT | O_EXCL, 0666, &attr);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[ARMINO] queue %u open failed: %d\n", q->id, ret);
      kmm_free(q);
      return NULL;
    }

  /* The open descriptor keeps it alive; the name is not needed. */

  file_mq_unlink(name);
  syslog(LOG_INFO, "[ARMINO] queue %u: item %lu depth %lu\n", q->id,
         (unsigned long)item_size, (unsigned long)len);
  return q;
}

QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t max,
                                            const UBaseType_t initial)
{
  return armino_sem_create(queueQUEUE_TYPE_COUNTING, max, initial);
}

/* Both mutex kinds use a recursive NuttX mutex: a FreeRTOS mutex taken
 * twice by its holder deadlocks, which Armino code never relies on.
 */

QueueHandle_t xQueueCreateMutex(const uint8_t type)
{
  QueueHandle_t q = kmm_zalloc(sizeof(*q));

  if (q != NULL)
    {
      q->type = type;
      nxrmutex_init(&q->u.mutex);
    }

  return q;
}

void vQueueDelete(QueueHandle_t q)
{
  if (q == NULL)
    {
      return;
    }

  if (armino_is_semaphore(q))
    {
      nxsem_destroy(&q->u.sem);
    }
  else if (armino_is_mutex(q))
    {
      nxrmutex_destroy(&q->u.mutex);
    }
  else
    {
      file_mq_close(&q->u.mq);
    }

  kmm_free(q);
}

static BaseType_t armino_sem_give(QueueHandle_t q)
{
  int value = 0;

  nxsem_get_value(&q->u.sem, &value);
  if (value >= (int)q->max_count)
    {
      return errQUEUE_FULL;
    }

  return nxsem_post(&q->u.sem) == OK ? pdPASS : pdFAIL;
}

static void armino_queue_hiwater(QueueHandle_t q)
{
  struct mq_attr attr;

  if (file_mq_getattr(&q->u.mq, &attr) == OK && attr.mq_curmsgs > q->hiwater)
    {
      q->hiwater = attr.mq_curmsgs;
      /* Routine fill level is not news: a34-deep queue reporting 2/34 then
       * 3/34 then 4/34 floods the console and buries what matters.  Log
       * only when the queue actually reaches capacity -- that is the wall
       * a caller feels.  hiwater only rises, so this fires once per queue.
       */

      if ((unsigned long)q->hiwater >= q->depth)
        {
          syslog(LOG_WARNING, "[ARMINO] queue %u FULL %ld/%lu\n", q->id,
                 q->hiwater, (unsigned long)q->depth);
        }
    }
}

static BaseType_t armino_queue_send(QueueHandle_t q, const void *item,
                                    TickType_t ticks, BaseType_t pos)
{
  unsigned int prio = pos == queueSEND_TO_FRONT ? 1 : 0;
  int ret;

  if (pos == queueOVERWRITE)
    {
      syslog(LOG_ERR, "[ARMINO] queue %u: overwrite unsupported\n", q->id);
      return pdFAIL;
    }

  /* A zero timeout is "try once" in FreeRTOS. NuttX's tick send with 0
   * still arms a watchdog and waits for it, so a full queue is answered
   * here. ponytail: check and send are not atomic; a slot freed in between
   * only costs that one try.
   */

  if (ticks == 0 && !up_interrupt_context() &&
      armino_queue_count(q) >= (long)q->depth)
    {
      return errQUEUE_FULL;
    }

  if (ticks == portMAX_DELAY && !up_interrupt_context())
    {
      ret = file_mq_send(&q->u.mq, item, q->item_size, prio);
    }
  else
    {
      ret = file_mq_ticksend(&q->u.mq, item, q->item_size, prio,
                             (sclock_t)ticks);
    }

  if (ret < 0)
    {
      syslog(LOG_WARNING, "[ARMINO] queue %u send failed: %d isr=%d\n",
             q->id, ret, up_interrupt_context());
      return errQUEUE_FULL;
    }

  armino_queue_hiwater(q);
  return pdPASS;
}

BaseType_t xQueueGenericSend(QueueHandle_t q, const void *const item,
                             TickType_t ticks, const BaseType_t pos)
{
  if (armino_is_semaphore(q))
    {
      return armino_sem_give(q);
    }

  if (armino_is_mutex(q))
    {
      return nxrmutex_unlock(&q->u.mutex) == OK ? pdPASS : pdFAIL;
    }

  return armino_queue_send(q, item, ticks, pos);
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t q, const void *const item,
                                    BaseType_t *const woken,
                                    const BaseType_t pos)
{
  if (woken != NULL)
    {
      *woken = pdFALSE;
    }

  if (armino_is_semaphore(q))
    {
      return armino_sem_give(q);
    }

  return armino_queue_send(q, item, 0, pos);
}

BaseType_t xQueueGiveFromISR(QueueHandle_t q, BaseType_t *const woken)
{
  if (woken != NULL)
    {
      *woken = pdFALSE;
    }

  return armino_sem_give(q);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *const buf, TickType_t ticks)
{
  ssize_t ret;

  /* Zero timeout: same reasoning as in armino_queue_send(). */

  if (ticks == 0 && armino_queue_count(q) == 0)
    {
      return pdFAIL;
    }

  if (ticks == portMAX_DELAY)
    {
      ret = file_mq_receive(&q->u.mq, buf, q->item_size, NULL);
    }
  else
    {
      ret = file_mq_tickreceive(&q->u.mq, buf, q->item_size, NULL,
                                (sclock_t)ticks);
    }

  return ret >= 0 ? pdPASS : pdFAIL;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t ticks)
{
  int ret;

  if (armino_is_mutex(q))
    {
      if (ticks == portMAX_DELAY)
        {
          ret = nxrmutex_lock(&q->u.mutex);
        }
      else if (ticks == 0)
        {
          ret = nxrmutex_trylock(&q->u.mutex);
        }
      else
        {
          ret = nxrmutex_ticklock(&q->u.mutex, ticks);
        }
    }
  else if (ticks == portMAX_DELAY)
    {
      ret = nxsem_wait_uninterruptible(&q->u.sem);
    }
  else if (ticks == 0)
    {
      ret = nxsem_trywait(&q->u.sem);
    }
  else
    {
      ret = nxsem_tickwait_uninterruptible(&q->u.sem, ticks);
    }

  return ret == OK ? pdPASS : pdFAIL;
}

static long armino_queue_count(QueueHandle_t q)
{
  struct mq_attr attr;
  int value = 0;

  if (armino_is_semaphore(q))
    {
      nxsem_get_value(&q->u.sem, &value);
      return value;
    }

  if (armino_is_mutex(q))
    {
      return 0;
    }

  return file_mq_getattr(&q->u.mq, &attr) == OK ? attr.mq_curmsgs : 0;
}

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t q)
{
  return armino_queue_count(q) == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t q)
{
  long limit = armino_is_semaphore(q) ? (long)q->max_count : (long)q->depth;

  return armino_queue_count(q) >= limit ? pdTRUE : pdFALSE;
}

/****************************************************************************
 * Software timers on HPWORK
 ****************************************************************************/

struct armino_timer_s;
typedef struct armino_timer_s *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);

struct armino_timer_s
{
  struct work_s work;
  TickType_t period;
  BaseType_t reload;
  void *id;
  TimerCallbackFunction_t cb;
  volatile bool active;
};

static void armino_timer_worker(void *arg)
{
  TimerHandle_t t = arg;

  if (!t->active)
    {
      return;
    }

  if (t->reload)
    {
      work_queue(HPWORK, &t->work, armino_timer_worker, t, t->period);
    }
  else
    {
      t->active = false;
    }

  t->cb(t);
}

TimerHandle_t xTimerCreate(const char *const name, const TickType_t period,
                           const BaseType_t reload, void *const id,
                           TimerCallbackFunction_t cb)
{
  TimerHandle_t t = kmm_zalloc(sizeof(*t));

  (void)name;
  if (t != NULL)
    {
      t->period = period;
      t->reload = reload;
      t->id = id;
      t->cb = cb;
    }

  return t;
}

BaseType_t xTimerGenericCommand(TimerHandle_t t, const BaseType_t cmd,
                                const TickType_t value,
                                BaseType_t *const woken,
                                const TickType_t ticks)
{
  (void)ticks;

  if (woken != NULL)
    {
      *woken = pdFALSE;
    }

  switch (cmd)
    {
      case tmrCOMMAND_CHANGE_PERIOD:
      case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
        t->period = value;

        /* FreeRTOS starts a timer whose period is changed. */

        /* fallthrough */

      case tmrCOMMAND_START:
      case tmrCOMMAND_RESET:
      case tmrCOMMAND_START_FROM_ISR:
      case tmrCOMMAND_START_FROM_ISR + 1:   /* tmrCOMMAND_RESET_FROM_ISR */
        work_cancel(HPWORK, &t->work);
        t->active = true;
        return work_queue(HPWORK, &t->work, armino_timer_worker, t,
                          t->period) == OK ? pdPASS : pdFAIL;

      case tmrCOMMAND_STOP:
      case tmrCOMMAND_STOP_FROM_ISR:
        t->active = false;
        work_cancel(HPWORK, &t->work);
        return pdPASS;

      case tmrCOMMAND_DELETE:

        /* ponytail: a callback already running on HPWORK may still hold
         * t; Armino deletes timers from its own task after stopping them.
         * Defer the free through HPWORK if that ever proves false.
         */

        t->active = false;
        work_cancel(HPWORK, &t->work);
        kmm_free(t);
        return pdPASS;

      default:
        syslog(LOG_ERR, "[ARMINO] timer command %ld unsupported\n",
               (long)cmd);
        return pdFAIL;
    }
}

BaseType_t xTimerIsTimerActive(TimerHandle_t t)
{
  return t->active ? pdTRUE : pdFALSE;
}

void *pvTimerGetTimerID(const TimerHandle_t t)
{
  return t->id;
}
