#ifndef HOSTSTUB_IRQ_H
#define HOSTSTUB_IRQ_H
#include <stdint.h>
typedef uint32_t irqstate_t;
irqstate_t up_irq_save(void);
void up_irq_restore(irqstate_t flags);
uint32_t getprimask(void);
int up_interrupt_context(void);
#endif
