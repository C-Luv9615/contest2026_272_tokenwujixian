/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_armino_shell.c
 *
 * NuttX lower half for the Armino shell (docs/compose/spec/
 * bk7258-wifi-native.md, S2.7). Armino's bk_cli stays the application
 * layer: cli_main.c and shell_task.c are compiled unchanged. Only the two
 * units below them that NuttX must own are replaced here:
 *
 *  - cp/components/bk_cli/shell_uart.c drove UART0 itself (bk_uart_init,
 *    its own RX/TX ISRs), the same UART NuttX uses for its console. This
 *    file provides the same shell_uart device on top of NuttX instead:
 *    output through up_putc(), no UART access of its own, no input (NuttX's
 *    NSH owns the console input).
 *  - cp/components/bk_cli/cli_rpc.c mixes Armino's driver/spinlock.h
 *    spinlock_t with NuttX's in one unit. Its only export used elsewhere is
 *    cli_debug_cmd_table() (cli_main.c:961, the "debug" sub-commands for the
 *    dual-core ipc/rpc/spin_lock tests), stubbed here as an empty table.
 *
 * Compiled with NuttX's flags, not Armino's, so it cannot include
 * shell_drv.h; the types below copy cp/components/bk_cli/shell_drv.h
 * (d5b0278) member for member. Both sides use the same short-enum ABI.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

/* shell_drv.h */

typedef enum
{
  bFALSE = 0,
  bTRUE  = !bFALSE,
} bool_t;

enum
{
  SHELL_IO_CTRL_GET_STATUS = 0,
  SHELL_IO_CTRL_RX_RESET,
  SHELL_IO_CTRL_TX_RESET,
  SHELL_IO_CTRL_FLUSH,
  SHELL_IO_CTRL_TX_SUSPEND,
  SHELL_IO_CTRL_TX_RESUME,
  SHELL_IO_CTRL_RX_SUSPEND,
  SHELL_IO_CTRL_RX_RESUME,
  SHELL_IO_CTRL_SET_UART_PORT,
  SHELL_IO_CTRL_GET_UART_PORT,
  SHELL_IO_CTRL_GET_RX_STATUS,
  SHELL_IO_CTRL_SET_RX_ISR,
  SHELL_IO_CTRL_SET_TX_CMPL_ISR,
};

#define SHELL_DEV_UART 0

struct _shell_dev_drv;

typedef struct
{
  struct _shell_dev_drv *dev_drv;
  uint8_t dev_type;
  void *dev_ext;
} shell_dev_t;

typedef void (*tx_complete_t)(uint8_t *pbuf, uint16_t tag);
typedef void (*rx_indicate_t)(void);

typedef struct _shell_dev_drv
{
  bool_t   (*init)(shell_dev_t *shell_dev);
  bool_t   (*open)(shell_dev_t *shell_dev, tx_complete_t tx_callback,
                   rx_indicate_t rx_callback);
  uint16_t (*write_async)(shell_dev_t *shell_dev, uint8_t *buf,
                          uint16_t len, uint16_t tag);
  uint16_t (*read)(shell_dev_t *shell_dev, uint8_t *buf, uint16_t len);
  uint16_t (*write_sync)(shell_dev_t *shell_dev, uint8_t *buf,
                         uint16_t len);
  uint16_t (*write_echo)(shell_dev_t *shell_dev, uint8_t *buf,
                         uint16_t len);
  bool_t   (*io_ctrl)(shell_dev_t *shell_dev, uint8_t cmd, void *param);
  bool_t   (*close)(shell_dev_t *shell_dev);
} shell_dev_drv_t;

/* Queue sizes from shell_uart.c:11-14; shell_task relies on the TX queue
 * filling up (write_async returns 0) at the same depth.
 */

#define TX_QUEUE_LEN    8
#define ECHO_BUFF_SIZE  64

#define SHELL_DRAIN_PRIORITY  60
#define SHELL_DRAIN_STACKSIZE 2048

struct shell_packet_s
{
  uint8_t *packet;
  uint16_t len;
  uint16_t tag;
};

struct shell_nuttx_ext_s
{
  struct shell_packet_s tx_list[TX_QUEUE_LEN];
  uint8_t list_in;
  uint8_t list_out;
  uint8_t echo_buff[ECHO_BUFF_SIZE];
  uint8_t echo_rd;
  uint8_t echo_wr;
  bool tx_suspend;
  bool draining;
  bool started;
  uint8_t uart_id;
  tx_complete_t tx_complete;
  rx_indicate_t rx_indicate;
  sem_t kick;
};

static struct shell_nuttx_ext_s g_shell_ext;

/* Writes queued output. shell_uart.c did this in its UART TX ISR; here a
 * kernel thread does it, so a long log never holds interrupts off. The
 * completion callback runs before the packet is dequeued, as in
 * shell_uart_tx_isr(), because shell_task's callback re-enters write_async.
 */

static void shell_drain(struct shell_nuttx_ext_s *ext)
{
  for (; ; )
    {
      struct shell_packet_s p;
      irqstate_t flags = enter_critical_section();

      if (ext->tx_suspend)
        {
          ext->draining = false;
          leave_critical_section(flags);
          return;
        }

      if (ext->echo_rd != ext->echo_wr)
        {
          uint8_t ch = ext->echo_buff[ext->echo_rd];

          ext->echo_rd = (ext->echo_rd + 1) % ECHO_BUFF_SIZE;
          leave_critical_section(flags);
          up_putc(ch);
          continue;
        }

      if (ext->list_out == ext->list_in)
        {
          ext->draining = false;
          leave_critical_section(flags);
          return;
        }

      p = ext->tx_list[ext->list_out];
      leave_critical_section(flags);

      for (uint16_t i = 0; i < p.len; i++)
        {
          up_putc(p.packet[i]);
        }

      if (ext->tx_complete != NULL)
        {
          ext->tx_complete(p.packet, p.tag);
        }

      flags = enter_critical_section();
      ext->list_out = (ext->list_out + 1) % TX_QUEUE_LEN;
      leave_critical_section(flags);
    }
}

static int shell_drain_thread(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  for (; ; )
    {
      nxsem_wait_uninterruptible(&g_shell_ext.kick);
      shell_drain(&g_shell_ext);
    }

  return 0;
}

/* Caller holds the critical section. */

static void shell_kick(struct shell_nuttx_ext_s *ext)
{
  if (!ext->tx_suspend && !ext->draining && ext->started)
    {
      ext->draining = true;
      nxsem_post(&ext->kick);
    }
}

static uint16_t shell_free_items(struct shell_nuttx_ext_s *ext)
{
  if (ext->list_out > ext->list_in)
    {
      return ext->list_out - ext->list_in;
    }

  return TX_QUEUE_LEN - ext->list_in + ext->list_out;
}

static bool_t shell_nuttx_init(shell_dev_t *dev)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;
  irqstate_t flags;
  int pid;

  if (ext->started)
    {
      return bTRUE;
    }

  nxsem_init(&ext->kick, 0, 0);
  pid = kthread_create("armino-shell", SHELL_DRAIN_PRIORITY,
                       SHELL_DRAIN_STACKSIZE, shell_drain_thread, NULL);
  if (pid < 0)
    {
      return bFALSE;
    }

  /* Output queued before the thread existed goes out now. */

  flags = enter_critical_section();
  ext->started = true;
  if (ext->list_out != ext->list_in || ext->echo_rd != ext->echo_wr)
    {
      shell_kick(ext);
    }

  leave_critical_section(flags);
  return bTRUE;
}

static bool_t shell_nuttx_open(shell_dev_t *dev, tx_complete_t tx_callback,
                               rx_indicate_t rx_callback)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;

  ext->tx_complete = tx_callback;
  ext->rx_indicate = rx_callback;
  return bTRUE;
}

static uint16_t shell_nuttx_write_async(shell_dev_t *dev, uint8_t *buf,
                                        uint16_t len, uint16_t tag)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;
  irqstate_t flags;

  /* A zero-length packet is still queued: shell_task frees the buffer from
   * the completion callback (shell_uart.c:369).
   */

  if (buf == NULL)
    {
      return 0;
    }

  flags = enter_critical_section();
  if (shell_free_items(ext) <= 1)
    {
      leave_critical_section(flags);
      return 0;
    }

  ext->tx_list[ext->list_in].packet = buf;
  ext->tx_list[ext->list_in].len = len;
  ext->tx_list[ext->list_in].tag = tag;
  ext->list_in = (ext->list_in + 1) % TX_QUEUE_LEN;
  shell_kick(ext);
  leave_critical_section(flags);
  return 1;
}

/* Console input belongs to NuttX's NSH, so the shell never receives any. */

static uint16_t shell_nuttx_read(shell_dev_t *dev, uint8_t *buf,
                                 uint16_t len)
{
  (void)dev;
  (void)buf;
  (void)len;
  return 0;
}

static uint16_t shell_nuttx_write_sync(shell_dev_t *dev, uint8_t *buf,
                                       uint16_t len)
{
  (void)dev;

  if (buf == NULL)
    {
      return 0;
    }

  for (uint16_t i = 0; i < len; i++)
    {
      up_putc(buf[i]);
    }

  return len;
}

static uint16_t shell_nuttx_write_echo(shell_dev_t *dev, uint8_t *buf,
                                       uint16_t len)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;
  uint16_t count = 0;
  irqstate_t flags;

  if (buf == NULL)
    {
      return 0;
    }

  flags = enter_critical_section();
  while (count < len &&
         (ext->echo_wr + 1) % ECHO_BUFF_SIZE != ext->echo_rd)
    {
      ext->echo_buff[ext->echo_wr] = buf[count++];
      ext->echo_wr = (ext->echo_wr + 1) % ECHO_BUFF_SIZE;
    }

  if (count > 0)
    {
      shell_kick(ext);
    }

  leave_critical_section(flags);
  return count;
}

static bool_t shell_nuttx_ctrl(shell_dev_t *dev, uint8_t cmd, void *param)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;
  irqstate_t flags;
  uint16_t free_items;

  switch (cmd)
    {
      case SHELL_IO_CTRL_GET_STATUS:
        if (param == NULL)
          {
            return bFALSE;
          }

        flags = enter_critical_section();
        free_items = shell_free_items(ext);
        leave_critical_section(flags);
        *(uint16_t *)param = free_items > 1 ? free_items - 1 : 0;
        break;

      case SHELL_IO_CTRL_TX_RESET:
        flags = enter_critical_section();
        ext->list_in = 0;
        ext->list_out = 0;
        leave_critical_section(flags);
        break;

      case SHELL_IO_CTRL_FLUSH:

        /* Assert and dump paths: write everything out in the caller's
         * context. ponytail: not serialized against the drain thread; if
         * it is mid-packet that packet can print twice. Acceptable on the
         * assert path only; serialize it if FLUSH gets a normal caller.
         */

        ext->tx_suspend = false;
        shell_drain(ext);
        break;

      case SHELL_IO_CTRL_TX_SUSPEND:
        ext->tx_suspend = true;
        break;

      case SHELL_IO_CTRL_TX_RESUME:
        flags = enter_critical_section();
        ext->tx_suspend = false;
        if (ext->list_out != ext->list_in || ext->echo_rd != ext->echo_wr)
          {
            shell_kick(ext);
          }

        leave_critical_section(flags);
        break;

      case SHELL_IO_CTRL_RX_RESET:
      case SHELL_IO_CTRL_RX_SUSPEND:
      case SHELL_IO_CTRL_RX_RESUME:
        break;

      case SHELL_IO_CTRL_SET_UART_PORT:
        if (param == NULL)
          {
            return bFALSE;
          }

        ext->uart_id = *(uint8_t *)param;
        break;

      case SHELL_IO_CTRL_GET_UART_PORT:
        if (param == NULL)
          {
            return bFALSE;
          }

        *(uint8_t *)param = ext->uart_id;
        break;

      case SHELL_IO_CTRL_GET_RX_STATUS:
        if (param == NULL)
          {
            return bFALSE;
          }

        *(uint16_t *)param = 0;
        break;

      case SHELL_IO_CTRL_SET_RX_ISR:
        ext->rx_indicate = (rx_indicate_t)param;
        break;

      case SHELL_IO_CTRL_SET_TX_CMPL_ISR:
        ext->tx_complete = (tx_complete_t)param;
        break;

      default:
        return bFALSE;
    }

  return bTRUE;
}

static bool_t shell_nuttx_close(shell_dev_t *dev)
{
  struct shell_nuttx_ext_s *ext = dev->dev_ext;

  ext->tx_complete = NULL;
  ext->rx_indicate = NULL;
  return bTRUE;
}

static const shell_dev_drv_t g_shell_nuttx_drv =
{
  .init        = shell_nuttx_init,
  .open        = shell_nuttx_open,
  .write_async = shell_nuttx_write_async,
  .read        = shell_nuttx_read,
  .write_sync  = shell_nuttx_write_sync,
  .write_echo  = shell_nuttx_write_echo,
  .io_ctrl     = shell_nuttx_ctrl,
  .close       = shell_nuttx_close,
};

/* The only symbol shell_uart.c exported (used by shell_task.c and
 * at_server's at_task.c). uart_id starts at CONFIG_UART_PRINT_PORT (0 in the
 * baseline sdkconfig); it is only reported back, never used for I/O.
 */

shell_dev_t shell_uart =
{
  .dev_drv  = (struct _shell_dev_drv *)&g_shell_nuttx_drv,
  .dev_type = SHELL_DEV_UART,
  .dev_ext  = &g_shell_ext,
};

/* cli_rpc.c replacement: no "debug" sub-commands. lookup_cmd_table() with
 * zero items reports "cmd: ... NOT found." (cli_main.c:112-130).
 */

const void *cli_debug_cmd_table(int *num)
{
  *num = 0;
  return NULL;
}
