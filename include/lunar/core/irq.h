#pragma once

#include <lunar/asm/flags.h>

static inline void local_irq_enable(void) {
	__asm__ volatile("sti" : : : "memory");
}

static inline void local_irq_disable(void) {
	__asm__ volatile("cli" : : : "memory");
}

typedef unsigned long irqflags_t;

/**
 * @brief Check if IRQ's are enabled
 * @param flags The IRQ flags
 */
static inline bool local_irq_enabled(irqflags_t flags) {
	return !!(flags & CPU_FLAG_INTERRUPT);
}

/**
 * @brief Save and disable local IRQ's
 * @return The previous IRQ state
 */
static inline irqflags_t local_irq_save(void) {
	irqflags_t flags = read_cpu_flags();
	local_irq_disable();
	return flags;
}

/**
 * @brief Restore the local IRQ state
 * @param flags The flags restore from
 */
static inline void local_irq_restore(irqflags_t flags) {
	if (local_irq_enabled(flags))
		local_irq_enable();
}
