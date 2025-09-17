#include <crescent/core/io.h>
#include <crescent/core/interrupt.h>
#include "i8259.h"

#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_DATA (PIC1 + 1)
#define PIC2_DATA (PIC2 + 1)

#define PIC_EOI 0x20

static void i8259_spurious_eoi(const struct isr* isr) {
	if (isr->irq.irq == 15)
		outb(PIC1, PIC_EOI);
}

int i8259_set_irq(struct isr* isr, int irq, struct cpu* cpu, bool masked) {
	(void)cpu;
	if (!masked || (irq != 7 && irq != 15))
		return -ENOSYS;

	isr->irq.irq = irq;
	isr->irq.cpu = NULL;
	isr->irq.eoi = i8259_spurious_eoi;
	isr->irq.set_masked = NULL;

	return 0;
}

#define ICW1_ICW4 0x01
#define ICW1_SINGLE 0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL 0x08
#define ICW1_INIT 0x10
#define ICW4_8086 0x01
#define ICW4_AUTO 0x02
#define ICW4_BUF_SLAVE 0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM 0x10

void i8259_init(void) {
	/* Start initiailization in cascade mode */
	outb(PIC1, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC2, ICW1_INIT | ICW1_ICW4);
	io_wait();

	/* Set vector offsets */
	outb(PIC1_DATA, I8259_VECTOR_OFFSET);
	io_wait();
	outb(PIC2_DATA, I8259_VECTOR_OFFSET + 8);
	io_wait();

	/* Tell PIC1 there is a PIC2 */
	outb(PIC1_DATA, 4);
	io_wait();
	outb(PIC2_DATA, 2); /* Tell PIC2 it's "cascade identity", whatever that means */
	io_wait();

	/* Put both PIC's into 8086 mode */
	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	/* Mask all interrupts */
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}
