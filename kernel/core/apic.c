#include <crescent/common.h>
#include <crescent/asm/msr.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vmm.h>

#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_DATA (PIC1 + 1)
#define PIC2_DATA (PIC2 + 1)

#define PIC_EOI 0x20

void i8259_spurious_eoi(const struct irq* irq) {
	if (irq->irq == 15)
		outb(PIC1, PIC_EOI);
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

static void i8259_init(void) {
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

enum apic_base_flags {
	APIC_BASE_BSP = (1 << 8),
	APIC_BASE_ENABLE = (1 << 11)
};

#define LVT_DELIVERY_NMI (0b100 << 8)
#define LVT_MASK (1 << 16)

static const struct acpi_madt_ops* madt_ops = NULL;
static u32 __iomem* lapic_address;

void apic_set_madt_ops(const struct acpi_madt_ops* ops) {
	madt_ops = ops;
}

u32 lapic_read(unsigned int reg) {
	u32 __iomem* io = (u32*)((u8*)lapic_address + reg);
	return readl(io);
}

void lapic_write(unsigned int reg, u32 x) {
	u32 __iomem* io = (u32*)((u8*)lapic_address + reg);
	writel(io, x);
}

static void ioapic_redtbl_write(u32 __iomem* ioapic, u8 entry, u8 vector, 
		u8 delivery, u8 destmode, u8 polarity, u8 trigger, u8 masked, u8 dest) {
	u32 x = vector;

	x |= (delivery & 0b111) << 8;
	x |= (destmode & 1) << 11;
	x |= (polarity & 1) << 13;
	x |= (trigger & 1) << 15;
	x |= (masked & 1) << 16;

	ioapic_write(ioapic, 0x10 + entry * 2, x);
	ioapic_write(ioapic, 0x11 + entry * 2, (u32)dest << 24);
}

void apic_eoi(const struct irq* irq) {
	(void)irq;
	lapic_write(LAPIC_REG_EOI, 0);
}

int apic_set_irq(u8 irq, u8 vector, u8 processor, bool masked) {
	u8 polarity = 1;
	u8 trigger = 0;

	const u8 type = __ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE;
	unsigned long count = madt_ops->get_entry_count(type);
	for (unsigned long i = 0; i < count; i++) {
		struct __acpi_madt_interrupt_source_override* override = madt_ops->get_entry(type, i);
		if (override->source != irq)
			continue;

		polarity = override->flags & 2 ? 1 : 0;
		trigger = override->flags & 8 ? 1 : 0;
		irq = override->gsi;
		break;
	}

	struct ioapic_desc* desc = madt_ops->get_ioapic_gsi(irq);
	if (unlikely(!desc))
		return -ENOENT;

	irq = irq - desc->base;
	ioapic_redtbl_write(desc->address, irq, vector, 0, 0, polarity, trigger, masked, processor);

	return 0;
}

int apic_bsp_init(void) {
	if (!madt_ops)
		return -ENOSYS;

	i8259_init();

	physaddr_t lapic_physical = ROUND_DOWN(rdmsr(MSR_APIC_BASE), PAGE_SIZE);
	lapic_address = iomap(lapic_physical, PAGE_SIZE, MMU_READ | MMU_WRITE);
	if (unlikely(!lapic_address))
		return -ENOMEM;

	unsigned long ioapic_count = madt_ops->get_entry_count(__ACPI_MADT_ENTRY_TYPE_IOAPIC);
	struct ioapic_desc* ioapics = madt_ops->get_ioapics();
	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct ioapic_desc* ioapic = &ioapics[i];
		for (u32 j = ioapic->base; j < ioapic->top; j++)
			ioapic_redtbl_write(ioapic->address, j - ioapic->base, 0xfe, 0, 0, 0, 0, 1, 0);
	}

	/* Setup spurious IRQ, the ISR is already set up */
	lapic_write(LAPIC_REG_SPURIOUS, INTERRUPT_SPURIOUS_VECTOR | 0x100);
	return 0;
}
