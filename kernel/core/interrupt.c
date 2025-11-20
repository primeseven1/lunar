#include <lunar/types.h>
#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/asm/segment.h>
#include <lunar/core/panic.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/printk.h>
#include <lunar/core/cpu.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/trace.h>
#include <lunar/core/softirq.h>
#include <lunar/core/apic.h>
#include <lunar/core/traps.h>
#include <lunar/init/status.h>
#include <lunar/mm/vmm.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/preempt.h>
#include <lunar/lib/string.h>
#include "internal.h"

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
		if (IS_PTR_ERR(idt))
			panic("Failed to map the interrupt table\n");
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

int interrupt_get_vector(const struct isr* isr) {
	uintptr_t base = (uintptr_t)&isr_handlers[0];
	uintptr_t end = base + (INTERRUPT_COUNT * sizeof(struct isr));
	if ((uintptr_t)isr < base || (uintptr_t)isr >= end)
		return INT_MAX;

	size_t off = (uintptr_t)isr - base;
	if (off % sizeof(struct isr) != 0)
		return INT_MAX;

	return (int)(off / sizeof(struct isr));
}

struct isr* interrupt_alloc(void) {
	irqflags_t irq;
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

int interrupt_free(struct isr* isr) {
	int vector = interrupt_get_vector(isr);
	if (vector == INT_MAX || vector < INTERRUPT_EXCEPTION_COUNT)
		return -EINVAL;

	irqflags_t irq;
	spinlock_lock_irq_save(&isr_free_list_lock, &irq);
	isr_free_list[vector] = false;
	spinlock_unlock_irq_restore(&isr_free_list_lock, &irq);

	return 0;
}

int interrupt_register(struct isr* isr, void (*func)(struct isr*, struct context*),
		int (*set_irq)(struct isr* isr, int irq, struct cpu* cpu, bool masked),
		int irq, struct cpu* cpu, bool masked) {
	if (interrupt_get_vector(isr) == INT_MAX)
		return -EINVAL;
	if (isr->func)
		return -EALREADY;

	isr->func = func;
	atomic_store_explicit(&isr->inflight, 0, ATOMIC_RELEASE);

	return set_irq(isr, irq, cpu, masked);
}

/* Must be called on the target CPU of the ISR */
static void interrupt_unregister_work(void* arg) {
	struct isr* isr = arg;
	struct semaphore* sem = isr->private;
	isr->irq.unset_irq(isr);
	semaphore_signal(sem);
}

static int __interrupt_unregister(struct isr* isr) {
	struct semaphore* sem = kmalloc(sizeof(*sem), MM_ZONE_NORMAL);
	if (!sem)
		return -ENOMEM;

	int err = isr->irq.set_masked(isr, true);
	if (err) {
		kfree(sem);
		return err;
	}
	bug(interrupt_synchronize(isr) != 0);

	semaphore_init(sem, 0);
	isr->private = sem; /* Safe, since the ISR is blocked from running after syncing */
	err = sched_workqueue_add_on(isr->irq.cpu, interrupt_unregister_work, isr);
	while (err == -EAGAIN) {
		schedule();
		err = sched_workqueue_add_on(isr->irq.cpu, interrupt_unregister_work, isr);
	}

	semaphore_wait(sem, 0);
	kfree(sem);
	return 0;
}

int interrupt_unregister(struct isr* isr) {
	if (init_status_get() < INIT_STATUS_SCHED)
		return -EWOULDBLOCK;

	/* Can't unregister interrupts that cannot be masked or have no real IRQ */
	if (interrupt_get_vector(isr) == INT_MAX ||
			isr->irq.irq == -1 || !isr->irq.set_masked)
		return -EINVAL;

	int err = __interrupt_unregister(isr);
	if (err == 0) {
		isr->private = NULL;
		isr->func = NULL;
	}
	return err;
}

int interrupt_synchronize(struct isr* isr) {
	if (init_status_get() < INIT_STATUS_SCHED)
		return -EWOULDBLOCK;

	/* Don't synchronize the interrupt unless it has a real IRQ assocated with it */
	int vector = interrupt_get_vector(isr);
	if (vector == INT_MAX || isr->irq.irq == -1)
		return -EINVAL;

	irqflags_t irq;
	spinlock_lock_irq_save(&isr->lock, &irq);

	if (atomic_load(&isr->inflight) >= 0)
		atomic_add_fetch(&isr->inflight, LONG_MIN);

	spinlock_unlock_irq_restore(&isr->lock, &irq);

	while (atomic_load(&isr->inflight) != LONG_MIN)
		schedule();

	return 0;
}

int interrupt_allow_entry_if_synced(struct isr* isr) {
	if (interrupt_get_vector(isr) == INT_MAX || isr->irq.irq == -1)
		return -EINVAL;

	irqflags_t irq;
	spinlock_lock_irq_save(&isr->lock, &irq);

	int err = 0;
	if (atomic_load(&isr->inflight) == LONG_MIN)
		atomic_store(&isr->inflight, 0);
	else
		err = -EBUSY;

	spinlock_unlock_irq_restore(&isr->lock, &irq);
	return err;
}

static inline bool irq_enter(struct isr* isr) {
	spinlock_lock(&isr->lock);

	bool can_enter = atomic_load(&isr->inflight) >= 0;
	if (can_enter) {
		preempt_offset(HARDIRQ_OFFSET);
		atomic_add_fetch(&isr->inflight, 1);
	}

	spinlock_unlock(&isr->lock);
	return can_enter;
}

static inline void irq_exit(struct isr* isr) {
	preempt_offset(-HARDIRQ_OFFSET);
	atomic_sub_fetch(&isr->inflight, 1);
}

static inline bool is_irq(const struct isr* isr) {
	return !!isr->irq.eoi;
}

static inline bool in_weird_interrupt(u8 vector) {
	return vector == INTERRUPT_NMI_VECTOR || vector == INTERRUPT_MACHINE_CHECK_VECTOR || vector == INTERRUPT_DOUBLE_FAULT_VECTOR;
}

static inline bool check_cpu(void) {
	return !rdmsr(MSR_GS_BASE);
}

static inline void swap_cpu(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

void __asmlinkage do_interrupt(struct context* ctx) {
	bug(ctx->vector >= INTERRUPT_COUNT);

	bool weird_interrupt = in_weird_interrupt(ctx->vector);
	bool bad_cpu = unlikely(weird_interrupt) ? check_cpu() : false;
	if (unlikely(bad_cpu))
		swap_cpu();

	const int init_status = init_status_get();

	struct isr* isr = &isr_handlers[ctx->vector];
	bool irq = is_irq(isr);
	bool can_enter = true;
	if (irq)
		can_enter = irq_enter(isr);
	else if (likely(!weird_interrupt)) {
		if (!local_irq_enabled(ctx->rflags))
			panic("Trap occurred in atomic context");
		local_irq_enable();
	}

	if (likely(can_enter)) {
		if (likely(isr->func))
			isr->func(isr, ctx);
		else
			printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", ctx->vector);
		if (irq)
			irq_exit(isr);
	}
	if (isr->irq.eoi)
		isr->irq.eoi(isr);

	local_irq_disable();

	/* VERY special context, if an NMI or MCE happens, nothing is safe to do */
	if (unlikely(weird_interrupt)) {
		if (bad_cpu)
			swap_cpu();
		return;
	}

	if (unlikely(init_status < INIT_STATUS_SCHED))
		return;

	struct thread* thread = current_thread();

	/* Make sure the CPU wasn't interrupted in a softirq context before executing more softirq's */
	if (!(thread->preempt_count & SOFTIRQ_MASK)) {
		preempt_offset(SOFTIRQ_OFFSET);
		local_irq_enable();
		do_pending_softirqs(false);
		preempt_offset(-SOFTIRQ_OFFSET);
		local_irq_disable();
	} else {
		return; /* In a softirq, resched is a bad idea */
	}

	if (current_cpu()->need_resched) {
		struct thread* prev = current_cpu()->runqueue.current;
		struct thread* next = atomic_schedule();
		if (next)
			atomic_context_switch(prev, next, ctx);
	}
}

__diag_pop();

static void nmi_isr(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	panic("NMI");
}

static void spurious_isr(struct isr* isr, struct context* ctx) {
	(void)ctx;
	static unsigned long counter = 0;

	counter++;
	if (interrupt_get_vector(isr) == INTERRUPT_SPURIOUS_VECTOR) {
		printk(PRINTK_WARN "core: spurious interrupt (count %lu)\n", counter);
	} else {
		printk(PRINTK_WARN "core: spurious interrupt on IRQ %hhu (count %lu)\n", isr->irq.irq, counter);
	}
}

static void double_fault_isr(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	panic("Double fault");
}

static void generic_trap_isr(struct isr* isr, struct context* ctx) {
	(void)ctx;
	panic("Unhandled exception: %i", interrupt_get_vector(isr));
}

static inline void trap_register(int vector, void (*func)(struct isr*, struct context*)) {
	bug(vector >= INTERRUPT_EXCEPTION_COUNT);
	isr_handlers[vector].func = func;
}

static inline void i8259_set_spurious(u8 irq) {
	u8 vector = I8259_VECTOR_OFFSET + irq;
	isr_free_list[vector] = true;
	isr_handlers[vector].func = spurious_isr;
	bug(i8259_set_irq(&isr_handlers[vector], irq, NULL, true) != 0);
}

static inline void apic_set_spurious(void) {
	isr_handlers[INTERRUPT_SPURIOUS_VECTOR].func = spurious_isr;
	isr_free_list[INTERRUPT_SPURIOUS_VECTOR] = true;
}

void interrupts_cpu_init(void) {
	idt_init();
}

void interrupts_init(void) {
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (i < INTERRUPT_EXCEPTION_COUNT) {
			isr_handlers[i].func = generic_trap_isr;
			isr_free_list[i] = true;
		}
		spinlock_init(&isr_handlers[i].lock);
		isr_handlers[i].irq.irq = -1;
	}

	trap_register(INTERRUPT_NMI_VECTOR, nmi_isr);
	trap_register(INTERRUPT_DOUBLE_FAULT_VECTOR, double_fault_isr);
	trap_register(INTERRUPT_GENERAL_PROTECTION_FAULT_VECTOR, gp_fault_isr);
	trap_register(INTERRUPT_PAGE_FAULT_VECTOR, page_fault_isr);

	i8259_set_spurious(7);
	i8259_set_spurious(15);
	apic_set_spurious();

	interrupts_cpu_init();
}
