#include <lunar/core/interrupt.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/core/intctl.h>
#include <lunar/mm/heap.h>

static struct intctl* intctl = NULL;

struct irq* intctl_install_irq(int irq, const struct isr* isr, struct cpu* cpu) {
	struct irq* irq_struct = kmalloc(sizeof(*irq_struct), MM_ZONE_NORMAL);
	if (!irq_struct)
		return ERR_PTR(-ENOMEM);

	irq_struct->number = irq;
	irq_struct->cpu = cpu;
	irq_struct->allow_entry = true;
	atomic_store(&irq_struct->inflight, 0);
	spinlock_init(&irq_struct->lock);

	irqflags_t irq_flags = local_irq_save();
	int err = intctl->ops->install(irq, isr, cpu);
	local_irq_restore(irq_flags);

	if (unlikely(err)) {
		kfree(irq_struct);
		printk(PRINTK_ERR "intctl: Failed to install handler for IRQ %d: %d\n", irq, err);
		return ERR_PTR(err);
	}
	return irq_struct;
}

int intctl_uninstall_irq(struct irq* irq) {
	irqflags_t irq_flags = local_irq_save();
	int err = intctl->ops->uninstall(irq->number);
	local_irq_restore(irq_flags);

	if (unlikely(err))
		printk(PRINTK_ERR "intctl: Failed to uninstall handler for IRQ %d: %d\n", irq->number, err);
	else
		kfree(irq);
	return err;
}

int intctl_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags) {
	irqflags_t irq_flags = local_irq_save();
	int err = intctl->ops->send_ipi(cpu, isr, flags);
	local_irq_restore(irq_flags);
	return err;
}

void intctl_enable_irq(const struct irq* irq) {
	irqflags_t irq_flags = local_irq_save();
	bug(intctl->ops->enable(irq->number) != 0);
	local_irq_restore(irq_flags);
}

void intctl_disable_irq(const struct irq* irq) {
	irqflags_t irq_flags = local_irq_save();
	bug(intctl->ops->disable(irq->number) != 0);
	local_irq_restore(irq_flags);
}

void intctl_eoi(const struct irq* irq) {
	int irq_num = irq ? irq->number : -1;
	bug(intctl->ops->eoi(irq_num) != 0);
}

void intctl_wait_pending(const struct irq* irq) {
	bug(intctl->ops->wait_pending(irq->number) != 0);
}

const struct intctl_timer* intctl_get_timer(void) {
	return intctl->timer;
}

extern struct intctl _ld_kernel_intctl_start[];
extern struct intctl _ld_kernel_intctl_end[];

static inline struct intctl* get_ctl(void) {
	struct intctl* ret = NULL;
	for (struct intctl* ctl = _ld_kernel_intctl_start; ctl < _ld_kernel_intctl_end; ctl++) {
		if (ctl->rating == 0)
			continue;
		if (!ret || ctl->rating > ret->rating)
			ret = ctl;
	}
	return ret;
}

void intctl_init_bsp(void) {
	while (1) {
		intctl = get_ctl();
		if (!intctl)
			panic("No interrupt controller");
		if (intctl->ops->init_bsp() == 0)
			break;
		intctl->rating = 0;
	}
	printk("intctl: Using %s\n", intctl->name);
}

void intctl_init_ap(void) {
	int err = intctl->ops->init_ap();
	if (err)
		panic("intctl->ops->init_ap() failed: %i", err);
}
