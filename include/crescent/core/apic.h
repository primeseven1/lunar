#pragma once

#include <crescent/core/io.h>
#include <crescent/core/interrupt.h>

/* 
 * The APIC handling code is built into the kernel, however since the kernel does not 
 * have builtin support for parsing ACPI tables, these are all double underscores, since
 * we want to allow the implementation to not have to worry about name collisions.
 */
enum __acpi_madt_entry_types {
	__ACPI_MADT_ENTRY_TYPE_LAPIC = 0,
	__ACPI_MADT_ENTRY_TYPE_IOAPIC = 1,
	__ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE = 2,
	__ACPI_MADT_ENTRY_TYPE_NMI_SOURCE = 3,
	__ACPI_MADT_ENTRY_TYPE_LAPIC_NMI = 4,
	__ACPI_MADT_ENTRY_TYPE_LAPIC_ADDRESS_OVERRIDE = 5,
	__ACPI_MADT_ENTRY_TYPE_IOSAPIC = 6,
	__ACPI_MADT_ENTRY_TYPE_LSAPIC = 7,
	__ACPI_MADT_ENTRY_TYPE_PLATFORM_INTERRUPT_SOURCES = 8,
	__ACPI_MADT_ENTRY_TYPE_GICC = 0xB,
	__ACPI_MADT_ENTRY_TYPE_GICD = 0xC,
	__ACPI_MADT_ENTRY_TYPE_GIC_MSI_FRAME = 0xD,
	__ACPI_MADT_ENTRY_TYPE_GICR = 0xE,
	__ACPI_MADT_ENTRY_TYPE_GIC_ITS = 0xF,
	__ACPI_MADT_ENTRY_TYPE_CORE_PIC = 0x11,
	__ACPI_MADT_ENTRY_TYPE_LIO_PIC = 0x12,
	__ACPI_MADT_ENTRY_TYPE_HT_PIC = 0x13,
	__ACPI_MADT_ENTRY_TYPE_EIO_PIC = 0x14,
	__ACPI_MADT_ENTRY_TYPE_MSI_PIC = 0x15,
	__ACPI_MADT_ENTRY_TYPE_BIO_PIC = 0x16,
	__ACPI_MADT_ENTRY_TYPE_LPC_PIC = 0x17,
	__ACPI_MADT_ENTRY_TYPE_RINTC = 0x18,
	__ACPI_MADT_ENTRY_TYPE_IMSIC = 0x19,
	__ACPI_MADT_ENTRY_TYPE_APLIC = 0x1A,
	__ACPI_MADT_ENTRY_TYPE_PLIC = 0x1B,
};

struct __acpi_entry_hdr {
	u8 type;
	u8 length;
} __attribute__((packed));

struct __acpi_madt_lapic_nmi {
	struct __acpi_entry_hdr hdr;
	u8 uid;
	u16 flags;
	u8 lint;
} __attribute__((packed));

struct __acpi_madt_interrupt_source_override {
	struct __acpi_entry_hdr hdr;
	u8 source;
	u8 bus;
	u32 gsi;
	u16 flags;
} __attribute__((packed));

struct ioapic_desc {
	u32 __iomem* address;
	u32 base, top;
};

struct acpi_madt_ops {
	unsigned long (*get_entry_count)(u8);
	void* (*get_entry)(u8, unsigned long);
	struct ioapic_desc* (*get_ioapic_gsi)(u32);
	struct ioapic_desc* (*get_ioapics)(void);
};

enum ioapic_regs { 
	IOAPIC_REG_ID = 0,
	IOAPIC_REG_ENTRY_COUNT = 1,
	IOAPIC_REG_PRIORITY = 2,
	IOAPIC_REG_ENTRY = 0x10
};

void apic_set_madt_ops(const struct acpi_madt_ops* ops);

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

int apic_set_irq(u8 irq, u8 vector, u8 processor, bool masked);
int apic_bsp_init(void);
void apic_eoi(const struct isr* isr);
