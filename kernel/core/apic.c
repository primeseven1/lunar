#include <crescent/common.h>
#include <crescent/asm/msr.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vmm.h>
#include <crescent/mm/heap.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

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

static u32 __iomem* lapic_address;
struct ioapic_desc {
	u32 __iomem* address;
	u32 base, top;
};
static struct ioapic_desc* ioapics;
static unsigned long ioapic_count;

static struct acpi_madt* madt = NULL;

static unsigned long get_entry_count(u8 type) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);

	unsigned long count = 0;
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type)
			count++;
		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return count;
}

static void* get_entry(u8 type, unsigned long n) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type) {
			if (n-- == 0)
				return entry;
		}

		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return NULL;
}

static struct ioapic_desc* get_ioapic_gsi(u32 gsi) {
	for (unsigned long i = 0; i < ioapic_count; i++) {
		if (ioapics[i].base <= gsi && ioapics[i].top > gsi)
			return &ioapics[i];
	}

	return NULL;
}

u32 lapic_read(unsigned int reg) {
	u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
	return readl(io);
}

void lapic_write(unsigned int reg, u32 x) {
	u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
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

	const u8 type = ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE;
	unsigned long count = get_entry_count(type);
	for (unsigned long i = 0; i < count; i++) {
		struct acpi_madt_interrupt_source_override* override = get_entry(type, i);
		if (override->source != irq)
			continue;

		polarity = override->flags & 2 ? 1 : 0;
		trigger = override->flags & 8 ? 1 : 0;
		irq = override->gsi;
		break;
	}

	struct ioapic_desc* desc = get_ioapic_gsi(irq);
	if (unlikely(!desc))
		return -ENOENT;
	
	irq = irq - desc->base;
	ioapic_redtbl_write(desc->address, irq, vector, 0, 0, polarity, trigger, masked, processor);

	return 0;
}

void apic_ap_init(void) {
	unsigned long nmi_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI);
	for (unsigned long i = 0; i < nmi_count; i++) {
		struct acpi_madt_lapic_nmi* nmi = get_entry(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI, i);
		if (nmi->uid == current_cpu()->processor_id || nmi->uid == 0xff) {
			lapic_write(LAPIC_REG_LVT_LINT0 + 0x10 * nmi->lint, 
					INTERRUPT_NMI_VECTOR | LVT_DELIVERY_NMI | (nmi->flags << 12));
		}
	}

	lapic_write(LAPIC_REG_SPURIOUS, INTERRUPT_SPURIOUS_VECTOR | 0x100);
}

static uacpi_status madt_init(void) {
	uacpi_table table;
	int err = uacpi_table_find_by_signature("APIC", &table);
	if (err != UACPI_STATUS_OK)
		return err;

	madt = table.ptr;

	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	ioapics = kmalloc(sizeof(*ioapics) * ioapic_count, MM_ZONE_NORMAL);
	if (!ioapics) {
		err = UACPI_STATUS_OUT_OF_MEMORY;
		goto err;
	}

	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct acpi_madt_ioapic* entry = get_entry(ACPI_MADT_ENTRY_TYPE_IOAPIC, i);
		if (unlikely(!entry)) {
			err = UACPI_STATUS_NOT_FOUND;
			goto err;
		}
		if (unlikely(entry->address % PAGE_SIZE != 0)) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}

		ioapics[i].address = iomap(entry->address, PAGE_SIZE, MMU_READ | MMU_WRITE);
		if (!ioapics[i].address) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}

		ioapics[i].base = entry->gsi_base;
		u32 _top = (ioapic_read(ioapics[i].address, IOAPIC_REG_ENTRY_COUNT) >> 16 & 0xFF) + 1;
		ioapics[i].top = ioapics[i].base + _top;
		printk(PRINTK_DBG "acpi: ioapic%lu at base: %u, top: %u\n", i, ioapics[i].base, ioapics[i].top);
	}

	return UACPI_STATUS_OK;
err:
	if (ioapics) {
		for (unsigned long i = 0; i < ioapic_count; i++) {
			if (ioapics[i].address)
				iounmap(ioapics[i].address, PAGE_SIZE);
		}
		kfree(ioapics);
	}
	madt = NULL;
	return err;
}

int apic_bsp_init(void) {
	uacpi_status err = madt_init();
	switch (err) {
	case UACPI_STATUS_MAPPING_FAILED:
	case UACPI_STATUS_OUT_OF_MEMORY:
		return -ENOMEM;
	case UACPI_STATUS_NOT_FOUND:
		return -ENOENT;
	case UACPI_STATUS_OK:
		break;
	default:
		return -EINVAL;
	}

	i8259_init();

	/* Get the physical memory address of the LAPIC and map it to virtual memory */
	physaddr_t lapic_physical = ROUND_DOWN(rdmsr(MSR_APIC_BASE), PAGE_SIZE);
	lapic_address = iomap(lapic_physical, PAGE_SIZE, MMU_READ | MMU_WRITE);
	if (unlikely(!lapic_address))
		return -ENOMEM;

	/* Mask all interrupts on the IOAPICs */
	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct ioapic_desc* ioapic = &ioapics[i];
		for (u32 j = ioapic->base; j < ioapic->top; j++)
			ioapic_redtbl_write(ioapic->address, j - ioapic->base, 0xfe, 0, 0, 0, 0, 1, 0);
	}

	apic_ap_init();
	return 0;
}
