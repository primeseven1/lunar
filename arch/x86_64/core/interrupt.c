#include <lunar/printk.h>
#include <lunar/panic.h>
#include <lunar/irq.h>
#include <lunar/sched.h>

#include <arch/irq_flags.h>
#include <arch/context.h>
#include <arch/asm/linkage.h>
#include <x86_64/idt.h>
#include <x86_64/fault.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/segment.h>

#include "internal.h"

#define EXCEPTION_COUNT 32
#define ISR_FLAG_EXCEPTION (1 << 0)
#define RFLAGS_IF (1 << 9)

extern const uintptr_t isr_table[ARCH_X86_64_IDT_ENTRY_COUNT];
static struct arch_x86_64_idt idt;
static atomic(struct isr*) isr_handlers[ARCH_X86_64_IDT_ENTRY_COUNT] = { 0 };
static SPINLOCK_DEFINE(isr_handlers_lock);

static struct isr exceptions[EXCEPTION_COUNT] = { 0 };
static struct isr i8259_spurious_irq7 = {
	.handler = arch_x86_64_i8259_spurious_isr, .private = NULL,
	.arch_specific = (struct arch_isr){
		.id = I8259_VECTOR_OFFSET + 7, .flags = 0, .ehandler = NULL, .need_eoi = false
	}
};
static struct isr i8259_spurious_irq15 = {
	.handler = arch_x86_64_i8259_spurious_isr, .private = NULL,
	.arch_specific = (struct arch_isr){
		.id = I8259_VECTOR_OFFSET + 15, .flags = 0, .ehandler = NULL, .need_eoi = false
	}
};

static inline bool in_weird_interrupt(int vector) {
	return (vector == ARCH_X86_64_IDT_NMI_VECTOR || vector == ARCH_X86_64_IDT_MACHINE_CHECK_VECTOR || vector == ARCH_X86_64_IDT_DOUBLE_FAULT_VECTOR);
}

static void register_exception(struct isr* isr) {
	switch (isr->arch_specific.id) {
	case ARCH_X86_64_IDT_PAGE_FAULT_VECTOR:
		isr->arch_specific.ehandler = arch_x86_64_page_fault;
		break;
	}
}

void arch_x86_64_idt_init(void) {
	static atomic(bool) idt_initialized = atomic_init(false);
	if (atomic_exchange(&idt_initialized, true) == false) {
		int ist = 0;
		for (size_t i = 0; i < ARRAY_SIZE(idt.entries); i++) {
			bool need_ist = in_weird_interrupt(i);
			struct arch_x86_64_idt_entry* entry = &idt.entries[i];
			*entry = (struct arch_x86_64_idt_entry){
				.handler_low = isr_table[i] & U16_MAX, .cs = ARCH_X86_64_SEGMENT_KERNEL_CODE,
				.ist = need_ist ? ++ist : 0, .flags = 0x8e, .handler_mid = (isr_table[i] >> 16) & U16_MAX,
				.handler_high = ((u64)isr_table[i] >> 32) & U32_MAX, ._zero = 0
			};
		}
		bug(ist != ARCH_X86_64_IDT_IST_COUNT);
		for (size_t i = 0; i < ARRAY_SIZE(exceptions); i++) {
			exceptions[i].arch_specific = (struct arch_isr){
				.id = i, .flags = ISR_FLAG_EXCEPTION, .ehandler = NULL, .need_eoi = false
			};
			register_exception(&exceptions[i]);
			atomic_store(&isr_handlers[i], &exceptions[i]);
		}
		atomic_store(&isr_handlers[I8259_VECTOR_OFFSET + 7], &i8259_spurious_irq7);
		atomic_store(&isr_handlers[I8259_VECTOR_OFFSET + 15], &i8259_spurious_irq15);
	}
	arch_x86_64_idt_reload(&idt, sizeof(idt));
}

static inline bool is_cpu_bad(void) {
	return !arch_x86_64_rdmsr(ARCH_X86_64_MSR_GS_BASE);
}

static inline void swapgs(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

static void handle_exception(struct isr* isr, struct arch_context* ctx) {
	/* If in an NMI/MCE/DF, gsbase may be wrong */
	bool weird_interrupt = in_weird_interrupt(isr->arch_specific.id);
	bool bad_cpu = unlikely(weird_interrupt) ? is_cpu_bad() : false;
	if (unlikely(bad_cpu))
		swapgs();

	if (unlikely(!isr->arch_specific.ehandler))
		panic("Exception %u occurred, but has no handler", isr->arch_specific.id);
	if ((unlikely(!(ctx->rflags & RFLAGS_IF) || current_thread()->preempt_count)) && !weird_interrupt)
		panic("Trap %u occurred in atomic context", isr->arch_specific.id);

	/* Don't re-enable interrupts if in an NMI/MCE/DF */
	if (!weird_interrupt)
		local_irq_enable();
	isr->arch_specific.ehandler(isr, ctx);
	local_irq_disable();

	/* Now just swap back to the way it was before if gsbase was wrong */
	if (unlikely(bad_cpu))
		swapgs();
}

void __asmlinkage arch_x86_64_do_interrupt(struct arch_context* ctx);
void __asmlinkage arch_x86_64_do_interrupt(struct arch_context* ctx) {
	struct isr* isr = atomic_load(&isr_handlers[ctx->vector]);
	if (unlikely(!isr))
		panic("Unregistered ISR %#lx", ctx->vector);

	if (isr->arch_specific.flags & ISR_FLAG_EXCEPTION) {
		handle_exception(isr, ctx);
	} else if (isr->handler) {
		preempt_offset(PREEMPT_HARDIRQ_OFFSET);
		isr->handler(isr);
		do_pending_irqs();
		preempt_offset(-PREEMPT_HARDIRQ_OFFSET);
	} else {
		printk(PRINTK_ERR "int%lu: No handler\n", ctx->vector);
	}

	if (isr->arch_specific.need_eoi)
		irqctl_eoi(isr);
	else if (in_weird_interrupt(ctx->vector))
		return;

	if (!(isr->arch_specific.flags & ISR_FLAG_EXCEPTION))
		softirq_execute();

	if (current_cpu()->need_resched && current_thread()->preempt_count == 0) {
		struct thread* current = current_thread();
		struct thread* next = atomic_schedule();
		if (next)
			arch_x86_64_context_switch_in_interrupt(current, next, ctx);
	}
}

int arch_register_isr(struct isr* isr) {
	int err = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&isr_handlers_lock, &irq_flags);

	int index = -1;
	for (size_t i = 0; i < ARRAY_SIZE(isr_handlers); i++) {
		if (index == -1 && atomic_load(&isr_handlers[i]) == NULL)
			index = i;
		if (atomic_load(&isr_handlers[i]) == isr) {
			err = -EEXIST;
			break;
		}
	}
	if (err == 0 && index != -1) {
		isr->arch_specific.id = index;
		isr->arch_specific.flags = 0;
		isr->arch_specific.need_eoi = true;
		atomic_store(&isr_handlers[index], isr);
	}

	spinlock_release_irq_restore(&isr_handlers_lock, &irq_flags);
	return err;
}

int arch_unregister_isr(struct isr* isr) {
	(void)isr;
	return -ENOSYS;
}

unsigned long arch_local_irq_read(void) {
	unsigned long flags;
	__asm__("pushfq\n\t"
			"popq %0"
			: "=r"(flags)
			:
			: "memory");
	return (flags & RFLAGS_IF) ? ARCH_IRQ_ENABLED : ARCH_IRQ_DISABLED;
}

void arch_local_irq_restore(unsigned long flags) {
	if (flags == ARCH_IRQ_ENABLED)
		__asm__ volatile("sti" : : : "memory");
	else
		__asm__ volatile("cli" : : : "memory");
}
