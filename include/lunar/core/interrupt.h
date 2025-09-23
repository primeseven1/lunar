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

struct isr;

struct irq {
	struct cpu* cpu; /* CPU this IRQ will run on, used by interrupt controller driver */
	int irq; /* IRQ the device uses, -1 for software IRQ */
	void (*eoi)(const struct isr*); /* End of interrupt signal */
	int (*set_masked)(const struct isr*, bool); /* Mask/unmask an interrupt */
	bool (*is_masked)(const struct isr*); /* Check if an interrupt is masked */
	void (*unset_irq)(struct isr*); /* Detach an IRQ. Must be called on the target CPU and be masked */
};

struct isr {
	struct irq irq; /* Set by the interrupt controller driver */
	void (*func)(struct isr*, struct context*); /* Called by the ISR entry */
	atomic(long) inflight; /* How many CPU's are running this ISR */
	spinlock_t lock; /* Lock for inflight, used when deciding whether the ISR can run */
	void* private; /* For use by whoever registers the interrupt */
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
 * @param func The function to execute
 * @param set_irq Interrupt controller set_irq (eg. apic_set_irq)
 * @param irq The IRQ this interrupt is associated with (-1 for software generated)
 * @param cpu The target CPU this IRQ should run on (NULL is valid for software generated IRQ's)
 * @param masked Whether or not the interrupt should be masked when registered
 *
 * @return -errno on failure
 */
int interrupt_register(struct isr* isr, void (*func)(struct isr*, struct context*),
		int (*set_irq)(struct isr* isr, int irq, struct cpu* cpu, bool masked),
		int irq, struct cpu* cpu, bool masked);

/**
 * @brief Unregister an interrupt
 *
 * Software generated IRQ's and exceptions cannot be unregistered.
 * Same with interrupts that cannot be masked.
 *
 * @param isr The ISR to unregister
 *
 * @return -errno on failure
 * @retval -EWOULDBLOCK Scheduler not initialized yet
 * @retval -EINVAL ISR is software generated, an exception, bad pointer, or cannot be masked
 * @retval -ENOMEM Ran out of memory trying to unregister the IRQ
 */
int interrupt_unregister(struct isr* isr);

/**
 * @brief Wait for ISR's to finish execution
 *
 * Not safe to call from an interrupt context.
 *
 * @param isr The ISR to wait for
 *
 * @retval -EINVAL ISR is invalid, an exception, or software triggered
 * @retval -EWOULDBLOCK Scheduler not initialized yet
 * @retval 0 Successful
 */
int interrupt_synchronize(struct isr* isr);

/**
 * @brief Re-allow entry into an ISR after interrupt_synchronize
 * @param isr The isr to allow entry in to
 *
 * @retval -EBUSY Not synced
 * @retval 0 Successful
 */
int interrupt_allow_entry_if_synced(struct isr* isr);

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
