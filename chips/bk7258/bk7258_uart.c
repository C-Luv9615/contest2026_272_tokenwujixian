/****************************************************************************
 * chips/bk7258/bk7258_uart.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/irq.h"

/* UART0's registers go through the same layer the rest of the firmware uses:
 * uart_hal.h / uart_ll.h.  The offsets were verified against
 * middleware/soc/bk7258/soc/uart_struct.h (dev_id@0x00, global_ctrl@0x08,
 * config@0x10, fifo_config@0x14, fifo_status@0x18, fifo_port@0x1c,
 * int_enable@0x20, int_status@0x24), and the FIFO bit positions match too
 * (tx_fifo_empty bit[17], fifo_wr_ready bit[20], fifo_rd_ready bit[21],
 * rx_fifo_data_out bits[8:15]), so this is the same hardware programming with
 * one implementation instead of two.
 *
 * The SoC interrupt router (BK7258_SYS_CPU0_INT_EN) is a different block and
 * has no uart_ll counterpart; those two accesses stay where they are.
 */

#include "uart_hal.h"
#include "uart_ll.h"

#define BK7258_UART0_ID  UART_ID_0

static uart_hw_t *const g_uart0_hw = (uart_hw_t *)BK7258_UART0_BASE;

struct bk7258_uart_s
{
  uint32_t ie;
};

static char g_rxbuffer[CONFIG_BK7258_UART0_RXBUFSIZE];
static char g_txbuffer[CONFIG_BK7258_UART0_TXBUFSIZE];
static struct bk7258_uart_s g_uartpriv;

static int bk7258_uart_setup(struct uart_dev_s *dev);
static void bk7258_uart_shutdown(struct uart_dev_s *dev);
static int bk7258_uart_attach(struct uart_dev_s *dev);
static void bk7258_uart_detach(struct uart_dev_s *dev);
static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg);
static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev);
static void bk7258_uart_send(struct uart_dev_s *dev, int ch);
static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_txready(struct uart_dev_s *dev);
static bool bk7258_uart_txempty(struct uart_dev_s *dev);
static int bk7258_uart_interrupt(int irq, void *context, void *arg);

static const struct uart_ops_s g_uart_ops =
{
  .setup       = bk7258_uart_setup,
  .shutdown    = bk7258_uart_shutdown,
  .attach      = bk7258_uart_attach,
  .detach      = bk7258_uart_detach,
  .ioctl       = bk7258_uart_ioctl,
  .receive     = bk7258_uart_receive,
  .rxint       = bk7258_uart_rxint,
  .rxavailable = bk7258_uart_rxavailable,
  .send        = bk7258_uart_send,
  .txint       = bk7258_uart_txint,
  .txready     = bk7258_uart_txready,
  .txempty     = bk7258_uart_txempty,
};

static struct uart_dev_s g_uartport =
{
  .isconsole = true,
  .recv =
  {
    .size = CONFIG_BK7258_UART0_RXBUFSIZE,
    .buffer = g_rxbuffer,
  },
  .xmit =
  {
    .size = CONFIG_BK7258_UART0_TXBUFSIZE,
    .buffer = g_txbuffer,
  },
  .ops = &g_uart_ops,
  .priv = &g_uartpriv,
};

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  (void)dev;
  bk7258_lowsetup();
  bk7258_gpio_uart0_rx();
  uart_ll_enable_rx(g_uart0_hw, BK7258_UART0_ID);
  return OK;
}

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  (void)dev;
  uart_ll_set_int_enable_status(g_uart0_hw, BK7258_UART0_ID, 0);
}

static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  int ret = irq_attach(BK7258_IRQ_UART0, bk7258_uart_interrupt, dev);
  if (ret == OK)
    {
      modifyreg32(BK7258_SYS_CPU0_INT_EN, 0, BK7258_SYS_UART0_INT_EN);
      up_enable_irq(BK7258_IRQ_UART0);
    }

  return ret;
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  (void)dev;
  uart_ll_set_int_enable_status(g_uart0_hw, BK7258_UART0_ID, 0);
  up_disable_irq(BK7258_IRQ_UART0);
  modifyreg32(BK7258_SYS_CPU0_INT_EN, BK7258_SYS_UART0_INT_EN, 0);
  irq_detach(BK7258_IRQ_UART0);
}

static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  (void)dev;
  *status = 0;
  return uart_ll_read_byte(g_uart0_hw, BK7258_UART0_ID);
}

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      priv->ie |= BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH;
    }
  else
    {
      priv->ie &= ~(BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH);
    }

  uart_ll_set_int_enable_status(g_uart0_hw, BK7258_UART0_ID, priv->ie);
}

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  (void)dev;
  return uart_ll_is_fifo_read_ready(g_uart0_hw, BK7258_UART0_ID);
}

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  (void)dev;
  bk7258_lowputc((char)ch);
}

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      /* Start the transfer immediately, then leave the hardware FIFO-ready
       * interrupt enabled so that the ISR drains any bytes which did not fit
       * in the FIFO on this first pass. */

      priv->ie |= BK7258_UART_INT_TX_READY;
      uart_ll_set_int_enable_status(g_uart0_hw, BK7258_UART0_ID, priv->ie);
      uart_xmitchars(dev);
    }
  else
    {
      priv->ie &= ~BK7258_UART_INT_TX_READY;
      uart_ll_set_int_enable_status(g_uart0_hw, BK7258_UART0_ID, priv->ie);
    }
}

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  (void)dev;
  return uart_ll_is_fifo_write_ready(g_uart0_hw, BK7258_UART0_ID);
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  (void)dev;
  return uart_ll_is_tx_fifo_empty(g_uart0_hw, BK7258_UART0_ID);
}

static int bk7258_uart_interrupt(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  struct bk7258_uart_s *priv = dev->priv;
  uint32_t raw;
  uint32_t status;

  (void)irq;
  (void)context;

  /* Clear every pending source, but dispatch only the ones this driver has
   * enabled.  The raw write-back has to stay unmasked: a bit left set in
   * INT_STATUS while its INT_ENABLE bit is clear would latch and re-trigger
   * the line forever.
   *
   * Filtering the dispatch is what matters here.  TX_READY reports FIFO write
   * space, so it is asserted almost all the time, including long after
   * bk7258_uart_txint(dev, false) has taken TX_READY out of priv->ie and out
   * of INT_ENABLE.  Without the mask, every RX interrupt also ran
   * uart_xmitchars() with an empty transmit buffer.
   *
   * That mattered because uart_xmitchars() takes uart_spinlock(dev, true),
   * i.e. spin_lock plus sched_lock, and the matching unlock runs
   * nxsched_unlock() -> nxsched_merge_pending() when the count returns to
   * zero, which can switch tasks from interrupt context.  Running that on
   * every RX interrupt rather than only when there is something to send is
   * what made an assert at drivers/serial/serial.c:2064 reachable during large
   * NSH writes (2 hits in 26 board runs, from ping and ifconfig output).
   *
   * The in-tree drivers gate the same way -- stm32_serial.c tests priv->ie
   * alongside the status bit in every arm of its ISR. */

  raw = uart_ll_get_interrupt_status(g_uart0_hw, BK7258_UART0_ID);
  uart_ll_clear_interrupt_status(g_uart0_hw, BK7258_UART0_ID, raw);

  status = raw & priv->ie;

  if (status & (BK7258_UART_INT_RX_READY | BK7258_UART_INT_RX_FINISH))
    {
      uart_recvchars(dev);
    }

  if (status & BK7258_UART_INT_TX_READY)
    {
      uart_xmitchars(dev);
    }

  return OK;
}

void arm_earlyserialinit(void)
{
  bk7258_lowsetup();
}

void arm_serialinit(void)
{
  /* The serial core treats a console as pre-initialized and skips setup() on
   * its first open. Complete RX pinmux and UART receive enable before
   * registering the console so NSH can receive input as well as emit early
   * polling output. */

  bk7258_lowputc('G');

  (void)bk7258_uart_setup(&g_uartport);

  bk7258_lowputc('H');

  uart_register("/dev/console", &g_uartport);
  uart_register("/dev/ttyS0", &g_uartport);

  bk7258_lowputc('I');
}

void up_putc(int ch)
{
  bk7258_lowputc((char)ch);
}
