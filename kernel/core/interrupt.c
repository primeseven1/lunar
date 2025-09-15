#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/segment.h>
#include <crescent/core/panic.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/trace.h>
#include <crescent/core/apic.h>
#include <crescent/mm/vmm.h>
#include <crescent/sched/scheduler.h>
#include <crescent/lib/string.h>
#include "traps.h"

struct idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 __reserved;
} __attribute__((packed));

static struct idt_entry* idt = NULL;
extern const uintptr_t isr_table[INTERRUPT_COUNT];

static void __idt_init(void) {
	for (size_t i = 0; i < INTERRUPT_COUNT; i++) {
		idt[i].handler_low = isr_table[i] & 0xFFFF;
		idt[i].cs = SEGMENT_KERNEL_CODE;
		idt[i].ist = i == 18 ? 1 : i == 8 ? 2 : i == 2 ? 3 : 0;
		idt[i].flags = 0x8e;
		idt[i].handler_mid = (isr_table[i] >> 16) & 0xFFFF;
		idt[i].handler_high = isr_table[i] >> 32;
		idt[i].__reserved = 0;
	}
}

static void idt_init(void) {
	const size_t idt_size = sizeof(*idt) * INTERRUPT_COUNT;
	if (!idt) {
		idt = vmap(NULL, idt_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
		assert(idt != NULL);
		__idt_init();
		vprotect(idt, idt_size, MMU_READ, 0);
	}

	struct {
		u16 limit;
		struct idt_entry* pointer;
	} __attribute__((packed)) idtr = {
		.limit = idt_size - 1,
		.pointer = idt
	};
	__asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

static struct isr isr_handlers[INTERRUPT_COUNT] = { 0 };
static bool isr_free_list[INTERRUPT_COUNT] = { 0 };
static SPINLOCK_DEFINE(isr_free_list_lock);

struct isr* interrupt_alloc(void) {
	unsigned long irq;
	spinlock_lock_irq_save(&isr_free_list_lock, &irq);

	struct isr* isr;
	size_t i;
	for (i = INTERRUPT_EXCEPTION_COUNT; i < ARRAY_SIZE(isr_handlers); i++) {
		isr = &isr_handlers[i];
		if (!isr_free_list[i]) {
			isr_free_list[i] = true;
			break;
		}
	}
	if (i == ARRAY_SIZE(isr_handlers))
		isr = NULL;

	spinlock_unlock_irq_restore(&isr_free_list_lock, &irq);
	return isr;
}

int interrupt_get_vector(const struct isr* isr) {
	uintptr_t base = (uintptr_t)&isr_handlers[0];
	uintptr_t end = base + (INTERRUPT_COUNT * sizeof(struct isr));
	if ((uintptr_t)isr < base || (uintptr_t)isr >= end)
		return INT_MAX;

	size_t off = (uintptr_t)isr - base;
	if (off % sizeof(struct isr) != 0)
		return INT_MAX;

	size_t index = off / sizeof(struct isr);
	return index;
}

int interrupt_free(struct isr* isr) {
	int vector = interrupt_get_vector(isr);
	if (vector == INT_MAX || vector < INTERRUPT_EXCEPTION_COUNT)
		return -EINVAL;

	unsigned long irq;
	spinlock_lock_irq_save(&isr_free_list_lock, &irq);
	isr_free_list[vector] = false;
	spinlock_unlock_irq_restore(&isr_free_list_lock, &irq);

	return 0;
}

void interrupt_register(struct isr* isr, struct irq* irq, void (*func)(struct isr*, struct context*)) {
	unsigned long irq_flags = local_irq_save();

	isr->irq = irq;
	isr->func = func;
	atomic_thread_fence(ATOMIC_RELEASE);

	local_irq_restore(irq_flags);
}

void interrupt_unregister(struct isr* isr) {
	unsigned long irq_flags = local_irq_save();

	isr->irq = NULL;
	isr->func = NULL;
	isr->private = NULL;
	atomic_thread_fence(ATOMIC_RELEASE);

	local_irq_restore(irq_flags);
}

static inline void swap_cpu(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

void __asmlinkage __isr_entry(struct context* ctx) {
	bool bad_cpu = false;
	if (ctx->vector == INTERRUPT_NMI_VECTOR || ctx->vector == INTERRUPT_MACHINE_CHECK_VECTOR) {
		u64 gsbase = rdmsr(MSR_GS_BASE);
		if (!gsbase) {
			swap_cpu();
			bad_cpu = true;
		}
	}

	bug(ctx->vector >= INTERRUPT_COUNT);

	struct isr* isr = &isr_handlers[ctx->vector];
	atomic_thread_fence(ATOMIC_ACQUIRE);
	if (likely(isr->func))
		isr->func(isr, ctx);
	else
		printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", ctx->vector);

	if (isr->irq) {
		assert(isr->irq->eoi != NULL);
		isr->irq->eoi(isr->irq);
	}

	if (unlikely(bad_cpu)) {
		swap_cpu();
		return;
	}

	if (current_cpu()->need_resched)
		schedule();
}

__diag_pop();

static void nmi(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	panic("NMI");
}

static void spurious(struct isr* isr, struct context* ctx) {
	(void)ctx;
	if (interrupt_get_vector(isr) == INTERRUPT_SPURIOUS_VECTOR) {
		printk(PRINTK_WARN "core: spurious interrupt from lapic\n");
	} else {
		printk(PRINTK_WARN "core: spurious interrupt from i8259 %hhu\n", isr->irq->irq);
	}
}

static struct irq irq7 = {
	.irq = 7,
	.eoi = i8259_spurious_eoi
};
static struct irq irq15 = {
	.irq = 15,
	.eoi = i8259_spurious_eoi
};

void interrupts_cpu_init(void) {
	idt_init();
}

void interrupts_init(void) {
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (i < INTERRUPT_EXCEPTION_COUNT) {
			isr_handlers[i].func = i == INTERRUPT_NMI_VECTOR ? nmi : do_trap;
			isr_free_list[i] = true;
		}
	}

	u8 irqv = I8259_VECTOR_OFFSET + 7;
	isr_free_list[irqv] = true;
	isr_handlers[irqv].irq = &irq7;
	isr_handlers[irqv].func = spurious;
	irqv = I8259_VECTOR_OFFSET + 15;
	isr_free_list[irqv] = true;
	isr_handlers[irqv].irq = &irq15;
	isr_handlers[irqv].func = spurious;

	isr_handlers[INTERRUPT_SPURIOUS_VECTOR].func = spurious;
	isr_free_list[INTERRUPT_SPURIOUS_VECTOR] = true;
	idt_init();
}
