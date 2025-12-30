#pragma once

#include <lunar/core/io.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/cpu.h>

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
	LAPIC_REG_LVT_LINT0 = 0x350,
	LAPIC_REG_LVT_LINT1 = 0x360,
	LAPIC_REG_ICR_LOW = 0x300,
	LAPIC_REG_ICR_HIGH = 0x310,
	LAPIC_REG_TIMER_INITIAL = 0x380,
	LAPIC_REG_TIMER_CURRENT = 0x390,
	LAPIC_REG_TIMER_DIVIDE = 0x3E0
};

u32 lapic_read(unsigned int reg);
void lapic_write(unsigned int reg, u32 x);
