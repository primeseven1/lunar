#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/segment.h>
#include <crescent/core/panic.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/apic.h>
#include <crescent/mm/vmm.h>
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
static spinlock_t isr_handlers_lock = SPINLOCK_INITIALIZER;

const struct isr* interrupt_register(struct irq* irq, void (*handler)(const struct isr*, struct context*)) {
	unsigned long flags;
	spinlock_lock_irq_save(&isr_handlers_lock, &flags);

	struct isr* isr = NULL;
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (!isr_handlers[i].handler) {
			isr = &isr_handlers[i];
			isr->irq = irq;
			isr->handler = handler;
			break;
		}
	}

	spinlock_unlock_irq_restore(&isr_handlers_lock, &flags);
	return isr;
}

static inline void swap_cpu(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage void __isr_entry(struct context* ctx) {
	bool bad_cpu = false;
	if (ctx->vector == INTERRUPT_NMI_VECTOR || ctx->vector == INTERRUPT_MACHINE_CHECK_VECTOR) {
		u64 gsbase = rdmsr(MSR_GS_BASE);
		if (!gsbase) {
			swap_cpu();
			bad_cpu = true;
		}
	}

	assert(ctx->vector < INTERRUPT_COUNT);
	struct isr* isr = &isr_handlers[ctx->vector];
	if (likely(isr->handler))
		isr->handler(isr, ctx);
	else
		printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", ctx->vector);

	if (isr->irq) {
		assert(isr->irq->eoi != NULL);
		isr->irq->eoi(isr->irq);
	}

	if (unlikely(bad_cpu))
		swap_cpu();
}

__diag_pop();

static void nmi(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	panic("NMI");
}

static void spurious(const struct isr* isr, struct context* ctx) {
	(void)ctx;
	if (isr->vector == INTERRUPT_SPURIOUS_VECTOR) {
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

void interrupts_init(void) {
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		isr_handlers[i].vector = i;
		if (i < INTERRUPT_EXCEPTION_COUNT)
			isr_handlers[i].handler = i == INTERRUPT_NMI_VECTOR ? nmi : do_trap;
	}

	int i8259_irq = I8259_VECTOR_OFFSET + 7;
	isr_handlers[i8259_irq].irq = &irq7;
	isr_handlers[i8259_irq].handler = spurious;
	i8259_irq = I8259_VECTOR_OFFSET + 15;
	isr_handlers[i8259_irq].irq = &irq15;
	isr_handlers[i8259_irq].handler = spurious;

	idt_init();
}
