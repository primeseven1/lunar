#pragma once

#include <crescent/asm/flags.h>
#include <crescent/core/locking.h>

struct context {
	void* cr2;
	long rax, rbx, rcx, rdx, rsi, rdi;
	void* rbp;
	long r8, r9, r10, r11, r12, r13, r14, r15;
	const void* __asm_isr_common_ret;
	unsigned long int_num, err_code;
	void* rip;
	unsigned long cs, rflags;
	void* rsp;
	unsigned long ss;
} __attribute__((packed));

struct isr {
	void (*handler)(const struct isr* self, const struct context* ctx);
	void (*eoi)(const struct isr* self);
	unsigned long int_num;
};

#define INTERRUPT_COUNT 256

const struct isr* interrupt_register(void (*handler)(const struct isr*, const struct context*));
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
