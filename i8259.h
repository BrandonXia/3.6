/* i8259.h - Defines used in interactions with the 8259 interrupt
 * controller
 * vim:ts=4 noexpandtab
 */

#ifndef _I8259_H
#define _I8259_H

#include "types.h"

typedef struct irq_type{
    unsigned ack;
} irq_type;

/* Ports that each PIC sits on */
#define MASTER_8259_PORT    0x20
#define SLAVE_8259_PORT     0xA0

/* Initialization control words to init each PIC.
 * See the Intel manuals for details on the meaning
 * of each word */
#define ICW1                0x11    // start init, edge-triggered inputs, cascade mode, 4ICWs
#define ICW2_MASTER         0x20    // high bits of vector
#define ICW2_SLAVE          0x28    // 
#define ICW3_MASTER         0x04    // primary PIC: bit vecotre of secondary PIC
#define ICW3_SLAVE          0x02    // secondary PIC: input pin on primary PIC
#define ICW4                0x01    // ISA=x86, normal/auto EOI

/* End-of-interrupt byte.  This gets OR'd with
 * the interrupt number and sent out to the PIC
 * to declare the interrupt finished */
#define EOI                 0x60

/*Total number of Ports*/
#define I8259_PORTS_NUMBER  8
/*The pin connected to the slave*/
#define MASTER_SLAVE_PIN    2


/* Externally-visible functions */

/* Initialize both PICs */
void i8259_init(void);
/* Enable (unmask) the specified IRQ */
void enable_irq(uint32_t irq_num);
/* Disable (mask) the specified IRQ */
void disable_irq(uint32_t irq_num);
/* Send end-of-interrupt signal for the specified IRQ */
void send_eoi(uint32_t irq_num);

#endif /* _I8259_H */
