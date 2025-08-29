#pragma once

#include <crescent/core/io.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/cpu.h>

enum ioapic_regs { 
	IOAPIC_REG_ID = 0,
	IOAPIC_REG_ENTRY_COUNT = 1,
	IOAPIC_REG_PRIORITY = 2,
	IOAPIC_REG_ENTRY = 0x10
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
	LAPIC_REG_LVT_TIMER = 0x320,
	LAPIC_REG_LVT_LINT0 = 0x350,
	LAPIC_REG_LVT_LINT1 = 0x360,
	LAPIC_REG_ICR_LOW = 0x300,
	LAPIC_REG_ICR_HIGH = 0x310,
	LAPIC_REG_TIMER_INITIAL = 0x380,
	LAPIC_REG_TIMER_CURRENT = 0x390,
	LAPIC_REG_TIMER_DIVIDE = 0x3E0
};

enum apic_ipitargets {
	APIC_IPI_CPU_TARGET = 0,
	APIC_IPI_CPU_SELF = 1,
	APIC_IPI_CPU_ALL = 2,
	APIC_IPI_CPU_OTHERS = 3
};

u32 lapic_read(unsigned int reg);
void lapic_write(unsigned int reg, u32 x);

/**
 * @brief Send an IPI to other processors
 *
 * @param target_cpu The target CPU to send the IPI to
 * @param isr The ISR to send
 * @param target A shorthand when sending to more than 1 CPU (APIC_IPI_CPU_*)
 * @param maskable If non-maskable, ISR is ignored.
 *
 * @retval -EINVAL Invalid targets value
 * @retval 0 Success
 */
int apic_send_ipi(struct cpu* target_cpu, const struct isr* isr, int targets, bool maskable);
int apic_set_irq(u8 irq, u8 vector, u8 processor, bool masked);
void apic_ap_init(void);
int apic_bsp_init(void);
void apic_eoi(const struct irq* irq);

#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

void i8259_spurious_eoi(const struct irq* irq);
