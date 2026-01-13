#include <lunar/common.h>
#include <lunar/asm/msr.h>
#include <lunar/asm/wrap.h>
#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include <lunar/core/io.h>
#include <lunar/core/printk.h>
#include <lunar/core/intctl.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/vmm.h>
#include <lunar/mm/heap.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#include "internal.h"

enum apic_base_flags {
	APIC_BASE_BSP = (1 << 8),
	APIC_BASE_ENABLE = (1 << 11)
};

static u32 __iomem* lapic_address;
struct ioapic_desc {
	u32 __iomem* address;
	u32 base, top;
};
static struct ioapic_desc* ioapics;
static unsigned long ioapic_count;
static SPINLOCK_DEFINE(ioapic_lock);

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

enum ioapic_regs { 
	IOAPIC_REG_ID = 0,
	IOAPIC_REG_ENTRY_COUNT = 1,
	IOAPIC_REG_PRIORITY = 2,
	IOAPIC_REG_REDTBL_BASE = 0x10
};

static inline u32 ioapic_read(u32 __iomem* ioapic, u8 reg) {
	u32 __iomem* io = ioapic;
	writel(io, reg);
	return readl(io + 4);
}

static inline void ioapic_write(u32 __iomem* ioapic, u8 reg, u32 x) {
	u32 __iomem* io = ioapic;
	writel(io, reg);
	writel(io + 4, x);
}

enum lapic_regs {
	LAPIC_REG_ID = 0x20,
	LAPIC_REG_EOI = 0xB0,
	LAPIC_REG_SPURIOUS = 0xF0,
	LAPIC_REG_ISR_BASE = 0x100,
	LAPIC_REG_TMR_BASE = 0x180,
	LAPIC_REG_IRR_BASE = 0x200,
	LAPIC_REG_LVT_TIMER = 0x320,
	LAPIC_REG_LVT_THERMAL = 0x330,
	LAPIC_REG_LVT_PERFORMANCE = 0x340,
	LAPIC_REG_LVT_LINT0 = 0x350,
	LAPIC_REG_LVT_LINT1 = 0x360,
	LAPIC_REG_LVT_ERROR = 0x370,
	LAPIC_X2_REG_ICR = 0x300,
	LAPIC_REG_ICR_LOW = 0x300,
	LAPIC_REG_ICR_HIGH = 0x310,
	LAPIC_REG_TIMER_INITIAL = 0x380,
	LAPIC_REG_TIMER_CURRENT = 0x390,
	LAPIC_REG_TIMER_DIVIDE = 0x3E0
};

enum lapic_reg_values {
	LAPIC_TIMER_DIVIDE_16 = 0x03,

	LAPIC_LVT_DELIVERY_NMI = (0b100 << 8),
	LAPIC_LVT_MASK = (1 << 16),
	LAPIC_LVT_TIMER_PERIODIC = (1 << 17),
	LAPIC_LVT_TIMER_TSC_DEADLINE = (1 << 18)
};

static inline u32 lapic_read(unsigned int reg) {
	u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
	return readl(io);
}

static inline void lapic_write(unsigned int reg, u32 x) {
	u32 __iomem* io = (u32 __iomem*)((u8 __iomem*)lapic_address + reg);
	writel(io, x);
}

struct ioapic_redtbl_entry {
	u8 vector;
	u8 delivery;
	u8 destmode;
	u8 polarity;
	u8 trigger;
	u8 masked;
	u8 dest;
};

static void ioapic_redtbl_write(u32 __iomem* ioapic, u8 entry, const struct ioapic_redtbl_entry* re) {
	u32 x = re->vector;

	x |= (re->delivery & 0b111) << 8;
	x |= (re->destmode & 1) << 11;
	x |= (re->polarity & 1) << 13;
	x |= (re->trigger & 1) << 15;
	x |= (re->masked & 1) << 16;

	ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + entry * 2, x);
	ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + 1 + (entry * 2), (u32)re->dest << 24);
}

static void ioapic_redtbl_read(u32 __iomem* ioapic, u8 entry, struct ioapic_redtbl_entry* out) {
	u32 low = ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + entry * 2);
	u32 high = ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + 1 + entry * 2);
	out->vector = low & 0xFF;
	out->delivery = (low >> 8) & 0b111;
	out->destmode = (low >> 11) & 1;
	out->polarity = (low >> 13) & 1;
	out->trigger = (low >> 15) & 1;
	out->masked = (low >> 16) & 1;
	out->dest = (high >> 24) & 0xFF;
}

static int apic_eoi(int irq) {
	(void)irq;
	lapic_write(LAPIC_REG_EOI, 0);
	return 0;
}

/* Sets an IRQ entry in the redirection table, the IOAPIC is expected to be locked */
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
	struct ioapic_redtbl_entry re = {
		.vector = vector,
		.masked = masked,
		.polarity = polarity,
		.trigger = trigger,
		.dest = processor,
		.destmode = 0,
		.delivery = 0
	};
	ioapic_redtbl_write(desc->address, irq, &re);

	return 0;
}

static int ioapic_get_irq(u8 irq, struct ioapic_redtbl_entry* re) {
	const u8 type = ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE;
	unsigned long count = get_entry_count(type);
	for (unsigned long i = 0; i < count; i++) {
		struct acpi_madt_interrupt_source_override* override = get_entry(type, i);
		if (override->source != irq)
			continue;

		irq = override->gsi;
		break;
	}

	struct ioapic_desc* desc = get_ioapic_gsi(irq);
	if (unlikely(!desc))
		return -ENOENT;

	irq = irq - desc->base;
	ioapic_redtbl_read(desc->address, irq, re);
	return 0;
}

/* Should be used with the IRR/ISR registers to check if an interrupt is pending or in service */
static inline bool __lapic_vec_test(unsigned int reg, u8 vector) {
	int bank = vector >> 5;
	u32 mask = 1u << (vector & 31);
	return (lapic_read(reg + bank * 0x10) & mask) != 0;
}

static inline bool lapic_is_pending(int vector) {
	return __lapic_vec_test(LAPIC_REG_IRR_BASE, vector);
}

static int apic_wait_pending(int irq) {
	struct ioapic_redtbl_entry re;

	irqflags_t irq_flags;
	spinlock_lock_irq_save(&ioapic_lock, &irq_flags);
	int err = ioapic_get_irq(irq, &re);
	spinlock_unlock_irq_restore(&ioapic_lock, &irq_flags);
	if (err)
		return err;

	while (lapic_is_pending(re.vector))
		cpu_relax();

	return 0;
}

static int apic_enable_irq(int irq) {
	struct ioapic_redtbl_entry re;

	spinlock_lock(&ioapic_lock);
	int err = ioapic_get_irq(irq, &re);
	if (err == 0)
		err = ioapic_set_irq(irq, re.vector, re.dest, false);
	spinlock_unlock(&ioapic_lock);

	return err;
}

static int apic_disable_irq(int irq) {
	struct ioapic_redtbl_entry re;

	spinlock_lock(&ioapic_lock);
	int err = ioapic_get_irq(irq, &re);
	if (err == 0)
		err = ioapic_set_irq(irq, re.vector, re.dest, true);
	spinlock_unlock(&ioapic_lock);

	return err;
}

static int apic_install(int irq, const struct isr* isr, const struct cpu* cpu) {
	int vector = interrupt_get_vector(isr);

	spinlock_lock(&ioapic_lock);
	int err = ioapic_set_irq(irq, vector, cpu->lapic_id, true);
	spinlock_unlock(&ioapic_lock);

	return err;
}

static int apic_uninstall(int irq) {
	spinlock_lock(&ioapic_lock);
	int err = ioapic_set_irq(irq, INTERRUPT_SPURIOUS_VECTOR, 0, true);
	spinlock_unlock(&ioapic_lock);

	return err;
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

enum apic_ipitargets {
	APIC_IPI_CPU_TARGET = 0,
	APIC_IPI_CPU_SELF = 1,
	APIC_IPI_CPU_ALL = 2,
	APIC_IPI_CPU_OTHERS = 3
};

static void lapic_x1_send_ipi(u32 cpu, u8 vector, u8 delivm, u8 trigger, u8 shorthand) {
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

static int apic_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags) {
	u32 id = cpu ? cpu->lapic_id : 0;
	u8 delivm = flags & INTCTL_IPI_CRITICAL ? APIC_DM_NMI : APIC_DM_FIXED;

	int vector = isr ? interrupt_get_vector(isr) : -1;
	if (delivm != APIC_DM_NMI && vector == -1)
		return -EINVAL;

	lapic_x1_send_ipi(id, vector, delivm, APIC_TRIGGER_EDGE, APIC_IPI_CPU_TARGET);
	return 0;
}

static int apic_ap_init(void) {
	lapic_write(LAPIC_REG_LVT_THERMAL, LAPIC_LVT_MASK);
	lapic_write(LAPIC_REG_LVT_PERFORMANCE, LAPIC_LVT_MASK);
	lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASK);
	lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_LVT_MASK);
	lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_LVT_MASK);
	lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASK);

	unsigned long nmi_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI);
	for (unsigned long i = 0; i < nmi_count; i++) {
		struct acpi_madt_lapic_nmi* nmi = get_entry(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI, i);
		if (nmi->uid == current_cpu()->processor_id || nmi->uid == 0xff) {
			lapic_write(LAPIC_REG_LVT_LINT0 + 0x10 * nmi->lint, 
					INTERRUPT_NMI_VECTOR | LAPIC_LVT_DELIVERY_NMI | (nmi->flags << 12));
		}
	}

	lapic_write(LAPIC_REG_SPURIOUS, INTERRUPT_SPURIOUS_VECTOR | 0x100);
	return 0;
}

static int madt_init(void) {
	uacpi_table table;
	uacpi_status err = uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &table);
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
		printk(PRINTK_DBG "apic: ioapic%lu at base: %u, top: %u\n", i, ioapics[i].base, ioapics[i].top);
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

	uacpi_table_unref(&table);
	madt = NULL;

	if (err == UACPI_STATUS_OUT_OF_MEMORY || err == UACPI_STATUS_MAPPING_FAILED)
		return -ENOMEM;
	return -ENOTSUP;
}

static void ioapic_mask_all(void) {
	i8259_disable();

	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct ioapic_desc* ioapic = &ioapics[i];
		const struct ioapic_redtbl_entry re = {
			.vector = INTERRUPT_SPURIOUS_VECTOR,
			.delivery = 0,
			.dest = 0,
			.masked = 1,
			.polarity = 0,
			.trigger = 0,
			.destmode = 0
		};
		for (u32 j = ioapic->base; j < ioapic->top; j++)
			ioapic_redtbl_write(ioapic->address, j - ioapic->base, &re);
	}
}

static int apic_x1_bsp_init(void) {
	int err = madt_init();
	if (err)
		return err;

	/* Get the physical memory address of the LAPIC and map it to virtual memory */
	physaddr_t lapic_physical = ROUND_DOWN(rdmsr(MSR_APIC_BASE), PAGE_SIZE);
	lapic_address = iomap(lapic_physical, PAGE_SIZE, MMU_READ | MMU_WRITE);
	if (unlikely(!lapic_address))
		return -ENOMEM;

	ioapic_mask_all();
	return apic_ap_init();
}

static const struct intctl_ops x1_ops = {
	.init_bsp = apic_x1_bsp_init,
	.init_ap = apic_ap_init,
	.install = apic_install,
	.uninstall = apic_uninstall,
	.send_ipi = apic_send_ipi,
	.enable = apic_enable_irq,
	.disable = apic_disable_irq,
	.eoi = apic_eoi,
	.wait_pending = apic_wait_pending
};

static int lapic_timer_setup(const struct isr* isr, time_t usec) {
	/* Slow down the timer, this also makes it easier to calibrate */
	lapic_write(LAPIC_REG_TIMER_DIVIDE, LAPIC_TIMER_DIVIDE_16);

	/* Now record the amount of ticks in the elapsed time period */
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX);
	timekeeper_stall(usec);
	u32 ticks = U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT);

	/* Now re-program the divide register and initial count, and set up the interrupt */
	lapic_write(LAPIC_REG_TIMER_DIVIDE, LAPIC_TIMER_DIVIDE_16);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);
	lapic_write(LAPIC_REG_LVT_TIMER, interrupt_get_vector(isr) | LAPIC_LVT_TIMER_PERIODIC);

	return 0;
}

static const struct intctl_timer_ops lapic_timer_ops = {
	.setup = lapic_timer_setup,
	.on_interrupt = NULL
};

static const struct intctl_timer lapic_timer = {
	.name = "lapic",
	.ops = &lapic_timer_ops
};

static struct intctl __intctl apic_x1 = {
	.name = "apic_x1",
	.rating = 75,
	.ops = &x1_ops,
	.timer = &lapic_timer
};
