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
#include <lunar/init/status.h>
#include <lunar/mm/vmm.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/preempt.h>
#include <lunar/lib/string.h>
#include "traps.h"
#include "i8259.h"

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

static bool irq_enter(struct isr* isr, int status, bool* nested) {
	*nested = false;

	struct thread* thread = current_thread();
	if (likely(status >= INIT_STATUS_SCHED)) {
		*nested = !!(thread->preempt_count & HARDIRQ_MASK);
		preempt_offset(HARDIRQ_OFFSET);
	}

	if (interrupt_get_vector(isr) < INTERRUPT_EXCEPTION_COUNT || isr->irq.irq == -1)
		return true;

	spinlock_lock(&isr->lock);

	bool can_enter = atomic_load(&isr->inflight) >= 0;
	if (can_enter)
		atomic_add_fetch(&isr->inflight, 1);
	else if (!*nested) /* Impossible for can_enter to be false when the scheduler isn't initialized */
		preempt_offset(-HARDIRQ_OFFSET);

	spinlock_unlock(&isr->lock);
	return can_enter;
}

static inline bool is_resched_a_bad_idea(u8 vector) {
	return vector == INTERRUPT_NMI_VECTOR || vector == INTERRUPT_MACHINE_CHECK_VECTOR || vector == INTERRUPT_DOUBLE_FAULT_VECTOR;
}

static void irq_exit(struct isr* isr, int status, bool nested) {
	if (likely(status >= INIT_STATUS_SCHED) && !nested)
		preempt_offset(-HARDIRQ_OFFSET);
	if (interrupt_get_vector(isr) >= INTERRUPT_EXCEPTION_COUNT && isr->irq.irq != -1)
		atomic_sub_fetch(&isr->inflight, 1);
}

static inline bool check_cpu(void) {
	return !rdmsr(MSR_GS_BASE);
}

static inline void swap_cpu(void) {
	__asm__ volatile("swapgs" : : : "memory");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

void __asmlinkage __isr_entry(struct context* ctx) {
	bug(ctx->vector >= INTERRUPT_COUNT);
	bool bad_cpu = unlikely(is_resched_a_bad_idea(ctx->vector)) ? check_cpu() : false;
	if (unlikely(bad_cpu))
		swap_cpu();

	int init_status = init_status_get();

	struct isr* isr = &isr_handlers[ctx->vector];
	bool nested;
	bool can_enter = irq_enter(isr, init_status, &nested);
	if (can_enter) {
		if (likely(isr->func))
			isr->func(isr, ctx);
		else
			printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", ctx->vector);
		irq_exit(isr, init_status, nested);
	}
	if (isr->irq.eoi)
		isr->irq.eoi(isr);

	if (unlikely(is_resched_a_bad_idea(ctx->vector))) {
		if (bad_cpu)
			swap_cpu();
		return;
	}

	if (unlikely(init_status < INIT_STATUS_SCHED))
		return;

	struct thread* thread = current_thread();

	/* Check to see if we were interrupted in a softirq. If so, don't run any softirqs and do NOT reschedule */
	if (!(thread->preempt_count & SOFTIRQ_MASK)) {
		preempt_offset(SOFTIRQ_OFFSET);
		local_irq_enable();
		do_pending_softirqs(false);
		preempt_offset(-SOFTIRQ_OFFSET);
		local_irq_disable();
		return;
	}

	if (current_cpu()->need_resched) {
		struct thread* prev = current_cpu()->runqueue.current;
		struct thread* next = atomic_schedule();
		if (next)
			atomic_context_switch(prev, next, ctx);
	}
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
		printk(PRINTK_WARN "core: spurious interrupt on IRQ %hhu\n", isr->irq.irq);
	}
}

void interrupts_cpu_init(void) {
	idt_init();
}

static inline void i8259_set_spurious(u8 irq) {
	u8 vector = I8259_VECTOR_OFFSET + irq;
	isr_free_list[vector] = true;
	isr_handlers[vector].func = spurious;
	bug(i8259_set_irq(&isr_handlers[vector], irq, NULL, true) != 0);
}

static inline void apic_set_spurious(void) {
	/* Don't register the IRQ, since no EOI needs to get sent */
	isr_handlers[INTERRUPT_SPURIOUS_VECTOR].func = spurious;
	isr_free_list[INTERRUPT_SPURIOUS_VECTOR] = true;
}

void interrupts_init(void) {
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (i < INTERRUPT_EXCEPTION_COUNT) {
			isr_handlers[i].func = i == INTERRUPT_NMI_VECTOR ? nmi : do_trap;
			isr_free_list[i] = true;
		}
		spinlock_init(&isr_handlers[i].lock);
		isr_handlers[i].irq.irq = -1;
	}

	i8259_set_spurious(7);
	i8259_set_spurious(15);
	apic_set_spurious();

	idt_init();
}
