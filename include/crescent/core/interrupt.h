#pragma once

#include <crescent/asm/flags.h>
#include <crescent/core/locking.h>

#define INTERRUPT_SPURIOUS_VECTOR 0xFF
#define INTERRUPT_EXCEPTION_COUNT 32

enum interrupt_exceptions {
	INTERRUPT_EXCEPTION_DIVIDE,
	INTERRUPT_EXCEPTION_DEBUG,
	INTERRUPT_EXCEPTION_NMI,
	INTERRUPT_EXCEPTION_BREAKPOINT,
	INTERRUPT_EXCEPTION_OVERFLOW,
	INTERRUPT_EXCEPTION_BOUND_RANGE,
	INTERRUPT_EXCEPTION_INVALID_OPCODE,
	INTERRUPT_EXCEPTION_NOFPU,
	INTERRUPT_EXCEPTION_DOUBLE_FAULT,
	INTERRUPT_EXCEPTION_BAD_TSS = 0xA,
	INTERRUPT_EXCEPTION_NO_SEGMENT,
	INTERRUPT_EXCEPTION_STACK_SEGMENT_FAULT,
	INTERRUPT_EXCEPTION_GENERAL_PROTECTION_FAULT,
	INTERRUPT_EXCEPTION_PAGE_FAULT,
	INTERRUPT_EXCEPTION_FPUEXCEPTION = 0x10,
	INTERRUPT_EXCEPTION_ALIGN_CHECK,
	INTERRUPT_EXCEPTION_MACHINE_CHECK,
	INTERRUPT_EXCEPTION_SIMD,
	INTERRUPT_EXCEPTION_VIRTUALIZATION,
	INTERRUPT_EXCEPTION_CONTROL_PROTECTION,
	INTERRUPT_EXCEPTION_HYPERVISOR = 0x28,
	INTERRUPT_EXCEPTION_VMM_COMMUNICATION,
	INTERRUPT_EXCEPTION_SECURITY
};

struct context {
	void* cr2;
	long rax, rbx, rcx, rdx, rsi, rdi;
	void* rbp;
	long r8, r9, r10, r11, r12, r13, r14, r15;
	const void* __asm_isr_common_ret;
	unsigned long vector, err_code;
	void* rip;
	unsigned long cs, rflags;
	void* rsp;
	unsigned long ss;
} __attribute__((packed));

struct irq {
	int irq;
	void (*eoi)(const struct irq*);
};

struct isr {
	struct irq* irq;
	unsigned int vector;
	void (*handler)(const struct isr*, struct context*);
};

#define INTERRUPT_COUNT 256

const struct isr* interrupt_register(struct irq* irq, void (*handler)(const struct isr*, struct context*));
void interrupts_init(void);

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

static inline unsigned long local_irq_save(void) {
	unsigned long flags = read_cpu_flags();
	local_irq_disable();
	return flags;
}

static inline void local_irq_restore(unsigned long flags) {
	if (flags & CPU_FLAG_INTERRUPT)
		local_irq_enable();
}
