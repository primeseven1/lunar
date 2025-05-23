#pragma once

#define CPU_FLAG_CARRY (1 << 0)
#define CPU_FLAG_PARITY (1 << 2)
#define CPU_FLAG_AUX_CARRY (1 << 4)
#define CPU_FLAG_ZERO (1 << 6)
#define CPU_FLAG_SIGN (1 << 7)
#define CPU_FLAG_TRAP (1 << 8)
#define CPU_FLAG_INTERRUPT (1 << 9)
#define CPU_FLAG_DIRECTION (1 << 10)
#define CPU_FLAG_OVERFLOW (1 << 11)
#define CPU_FLAG_IOPL1 (1 << 12)
#define CPU_FLAG_IOPL2 (1 << 13)
#define CPU_FLAG_NESTED_TASK (1 << 14)
#define CPU_FLAG_RESUME (1 << 16)
#define CPU_FLAG_V8086 (1 << 17)
#define CPU_FLAG_ALIGN_CHECK (1 << 18)
#define CPU_FLAG_VIRT_INTERRUPT (1 << 19)
#define CPU_FLAG_VIRT_INTERRUPT_PENDING (1 << 20)
#define CPU_FLAG_ID (1 << 21)

#ifndef __ASSEMBLER__

#include <crescent/types.h>

/**
 * @brief Read the CPU flags register
 * @return The value of the CPU flags register
 */
static inline unsigned long read_cpu_flags(void) {
	unsigned long flags;
	__asm__("pushfq\n\t" 
			"popq %0" 
			: "=r"(flags) 
			: 
			: "memory");
	return flags;
}

/**
 * @brief Enable interrupts on the current processor
 */
static inline void local_irq_enable(void) {
	__asm__ volatile("sti" : : : "memory");
}

/**
 * @brief Disable interrupts on the current processor
 */
static inline void local_irq_disable(void) {
	__asm__ volatile("cli" : : : "memory");
}

#endif /* __ASSEMBLER__ */
