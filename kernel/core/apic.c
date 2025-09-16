#include <crescent/common.h>
#include <crescent/asm/msr.h>
#include <crescent/asm/wrap.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vmm.h>
#include <crescent/mm/heap.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include "i8259.h"

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

static void apic_eoi(const struct isr* isr) {
	(void)isr;
	lapic_write(LAPIC_REG_EOI, 0);
}

static int ioapic_set_irq(u8 irq, u8 vector, u8 processor, bool masked) {
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

static int apic_set_mask(const struct isr* isr, bool masked) {
	int vector = interrupt_get_vector(isr);
	if (vector == INT_MAX || vector < INTERRUPT_EXCEPTION_COUNT)
		return -EINVAL;
	int err = ioapic_set_irq(isr->irq.irq, vector, isr->irq.cpu->lapic_id, masked);
	return err;
}

int apic_set_irq(struct isr* isr, int irq, struct cpu* cpu, bool masked) {
	int vector = interrupt_get_vector(isr);
	if (vector == INT_MAX || vector < INTERRUPT_EXCEPTION_COUNT)
		return -EINVAL;

	int err = ioapic_set_irq(irq, vector, cpu->lapic_id, masked);
	if (err == 0) {
		isr->irq.cpu = cpu;
		isr->irq.irq = irq;
		isr->irq.eoi = apic_eoi;
		isr->irq.set_mask = apic_set_mask;
	}
	return err;
}

int apic_set_noirq(struct isr* isr) {
	isr->irq.cpu = NULL;
	isr->irq.irq = -1;
	isr->irq.eoi = apic_eoi;
	isr->irq.set_mask = NULL;
	return 0;
}

#define LAPIC_ICR_DELIV_STATUS (1u << 12)

enum apic_delivery_modes {
	APIC_DM_FIXED,
	APIC_DM_NMI = 4
};

enum apic_dest_modes {
	APIC_DEST_PHYSICAL,
	APIC_DEST_LOGICAL
};

enum apic_triggers {
	APIC_TRIGGER_EDGE,
	APIC_TRIGGER_LEVEL
};

static void lapic_send_ipi(u32 cpu, u8 vector, u8 delivm, u8 trigger, u8 shorthand) {
	while (lapic_read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_DELIV_STATUS)
		cpu_relax();

	if (shorthand == APIC_IPI_CPU_TARGET)
		lapic_write(LAPIC_REG_ICR_HIGH, (cpu & 0xFF) << 24);

	u32 low = vector;
	low |= ((u32)delivm & 7) << 8;
	low |= ((u32)APIC_DEST_PHYSICAL) << 11;
	low |= ((u32)trigger & 1) << 15;
	low |= ((u32)shorthand & 3) << 18;
	lapic_write(LAPIC_REG_ICR_LOW, low);

	while (lapic_read(LAPIC_REG_ICR_LOW) & LAPIC_ICR_DELIV_STATUS)
		cpu_relax();
}

int apic_send_ipi(struct cpu* target_cpu, const struct isr* isr, int targets, bool maskable) {
	if (targets != APIC_IPI_CPU_TARGET && targets != APIC_IPI_CPU_ALL &&
			targets != APIC_IPI_CPU_OTHERS && targets != APIC_IPI_CPU_SELF)
		return -EINVAL;

	u32 id = target_cpu ? target_cpu->lapic_id : 0;
	u8 delivm = maskable ? APIC_DM_FIXED : APIC_DM_NMI;

	u8 vector = isr ? interrupt_get_vector(isr) : 0;
	if (maskable && vector == 0)
		return -EINVAL;

	unsigned long irq = local_irq_save();
	lapic_send_ipi(id, vector, delivm, APIC_TRIGGER_EDGE, targets);
	local_irq_restore(irq);

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
