#pragma once

#include <crescent/asm/flags.h>
#include <crescent/core/spinlock.h>

#define INTERRUPT_EXCEPTION_COUNT 32

enum interrupt_vectors {
	INTERRUPT_DIVIDE_VECTOR,
	INTERRUPT_DEBUG_VECTOR,
	INTERRUPT_NMI_VECTOR,
	INTERRUPT_BREAKPOINT_VECTOR,
	INTERRUPT_OVERFLOW_VECTOR,
	INTERRUPT_BOUND_RANGE_VECTOR,
	INTERRUPT_INVALID_OPCODE_VECTOR,
	INTERRUPT_NOFPU_VECTOR,
	INTERRUPT_DOUBLE_FAULT_VECTOR,
	INTERRUPT_BAD_TSS_VECTOR = 0xA,
	INTERRUPT_NO_SEGMENT_VECTOR,
	INTERRUPT_STACK_SEGMENT_FAULT_VECTOR,
	INTERRUPT_GENERAL_PROTECTION_FAULT_VECTOR,
	INTERRUPT_PAGE_FAULT_VECTOR,
	INTERRUPT_FPUEXCEPTION_VECTOR = 0x10,
	INTERRUPT_ALIGN_CHECK_VECTOR,
	INTERRUPT_MACHINE_CHECK_VECTOR,
	INTERRUPT_SIMD_EXCEPTION_VECTOR,
	INTERRUPT_VIRTUALIZATION_EXCEPTION_VECTOR,
	INTERRUPT_CONTROL_PROTECTION_VECTOR,
	INTERRUPT_HYPERVISOR_INJECT_VECTOR = 0x28,
	INTERRUPT_VMM_COMMUNICATION_VECTOR,
	INTERRUPT_SECURITY_VECTOR,
	INTERRUPT_SPURIOUS_VECTOR = 0xFF,
};

struct context {
	void* cr2;
	long rax, rbx, rcx, rdx, rsi, rdi;
	void* rbp;
	long r8, r9, r10, r11, r12, r13, r14, r15;
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
	void (*func)(struct isr*, struct context*);
	void* private;
};

#define INTERRUPT_COUNT 256

/**
 * @brief Allocate an interrupt
 */
struct isr* interrupt_alloc(void);

/**
 * @brief Free an interrupt structure
 * @param isr The ISR to free
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid ISR
 */
int interrupt_free(struct isr* isr);

/**
 * @brief Register an interrupt
 *
 * @param isr The ISR to register
 * @param irq The IRQ (NULL is allowed)
 * @param func The function to execute on interrupt
 */
void interrupt_register(struct isr* isr, struct irq* irq, void (*func)(struct isr*, struct context*));

/**
 * @brief Unregister an interrupt
 * @param isr The ISR to unregister
 */
void interrupt_unregister(struct isr* isr);

/**
 * @brief Get the interrupt vector for an interrupt
 * @param isr The ISR
 * @return INT_MAX on invalid ISR, otherwise the vector is returned
 */
int interrupt_get_vector(const struct isr* isr);

void interrupts_cpu_init(void);
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
