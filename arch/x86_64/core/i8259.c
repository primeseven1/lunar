#include <lunar/irq.h>
#include <x86_64/pmio.h>

#include "internal.h"

#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_DATA (PIC1 + 1)
#define PIC2_DATA (PIC2 + 1)

#define PIC_EOI 0x20

void i8259_spurious_isr(struct isr* isr) {
	int irqnum = isr->arch_specific.id - I8259_VECTOR_OFFSET;
	if (irqnum == 15)
		arch_x86_64_outb(PIC1, PIC_EOI);
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

void i8259_disable(void) {
	/* Start initiailization in cascade mode */
	arch_x86_64_outb(PIC1, ICW1_INIT | ICW1_ICW4);
	arch_x86_64_pmio_delay();
	arch_x86_64_outb(PIC2, ICW1_INIT | ICW1_ICW4);
	arch_x86_64_pmio_delay();

	/* Set vector offsets */
	arch_x86_64_outb(PIC1_DATA, I8259_VECTOR_OFFSET);
	arch_x86_64_pmio_delay();
	arch_x86_64_outb(PIC2_DATA, I8259_VECTOR_OFFSET + 8);
	arch_x86_64_pmio_delay();

	/* Tell PIC1 there is a PIC2 */
	arch_x86_64_outb(PIC1_DATA, 4);
	arch_x86_64_pmio_delay();
	arch_x86_64_outb(PIC2_DATA, 2); /* Tell PIC2 it's "cascade identity", whatever that means */
	arch_x86_64_pmio_delay();

	/* Put both PIC's into 8086 mode */
	arch_x86_64_outb(PIC1_DATA, ICW4_8086);
	arch_x86_64_pmio_delay();
	arch_x86_64_outb(PIC2_DATA, ICW4_8086);
	arch_x86_64_pmio_delay();

	/* Mask all interrupts */
	arch_x86_64_outb(PIC1_DATA, 0xFF);
	arch_x86_64_outb(PIC2_DATA, 0xFF);
}
