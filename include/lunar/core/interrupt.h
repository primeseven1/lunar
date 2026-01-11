#pragma once

#include <lunar/asm/flags.h>
#include <lunar/core/spinlock.h>

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
	u64 ds, es;
	u64 cr2;
	u64 rax, rbx, rcx, rdx, rsi, rdi;
	u64 rbp;
	u64 r8, r9, r10, r11, r12, r13, r14, r15;
	u64 vector, err_code;
	u64 rip;
	u64 cs, rflags;
	u64 rsp;
	u64 ss;
} __attribute__((packed));

struct isr;

struct irq {
	struct cpu* cpu; /* CPU this IRQ will run on */
	int number; /* IRQ the device uses */
	bool allow_entry; /* Allow entry into the ISR function */
	atomic(unsigned long) inflight; /* How many handlers are running */
	spinlock_t lock; /* For deciding if the IRQ should run */
};

struct isr {
	struct irq* irq; /* IRQ that this ISR handles */
	void (*func)(struct isr*, struct context*); /* Called by the ISR entry */
	bool need_eoi; /* If set, intctl_eoi() is called by the ISR entry. irq can be NULL */
	void* private; /* For use by whoever registers the interrupt */
};

#define INTERRUPT_COUNT 256

/**
 * @brief Allocate an interrupt
 *
 * This function tries to allocate an interrupt from one of the
 * available 256 vectors. After allocating, register the interrupt with
 * interrupt_register().
 *
 * @return Unlikely to return NULL
 */
struct isr* interrupt_alloc(void);

/**
 * @brief Free an interrupt
 *
 * Make sure to call interrupt_unregister() first!
 *
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
 * @param func The function to execute
 *
 * @retval 0 Success
 * @retval -EEXIST Interrupt handler already registered
 * @retval -EINVAL Invalid ISR
 */
int interrupt_register(struct isr* isr, struct irq* irq, void (*func)(struct isr*, struct context*));

/**
 * @brief Unregister an interrupt
 *
 * This function does NOT do any synchronization before unregistering.
 * Make sure it's safe to do so before calling this function.
 *
 * @param isr The ISR to unregister
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid ISR
 */
int interrupt_unregister(struct isr* isr);

/**
 * @brief Wait for ISR's to finish execution
 *
 * Not safe to call from an interrupt context. This function does NOT wait
 * for pending softirq's or work scheduled with sched_workqueue_add
 *
 * @param isr The ISR to wait for
 *
 * @retval -EINVAL ISR is invalid, an exception, or software triggered
 * @retval -EWOULDBLOCK Scheduler not initialized yet
 * @retval 0 Successful
 */
int interrupt_synchronize(struct isr* isr);

/**
 * @brief Get the interrupt vector for an interrupt
 * @param isr The ISR
 * @return -1 on invalid ISR, otherwise the vector is returned
 */
int interrupt_get_vector(const struct isr* isr);

void irq_synchronize(struct irq* irq);
void irq_disable(struct irq* irq);
void irq_enable(struct irq* irq);

void interrupts_cpu_init(void);
void interrupts_init(void);
