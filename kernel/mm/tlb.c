#include <crescent/asm/wrap.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/cpu.h>
#include <crescent/core/apic.h>
#include "pagetable.h"

static atomic(void*) shootdown_address;
static atomic(size_t) shootdown_size;
static atomic(u64) shootdown_remaining = atomic_static_init(0);
static spinlock_t shootdown_lock;

static const struct isr* shootdown_isr;
static struct irq shootdown_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

#define KERNEL_SPACE_START ((void*)0xFFFF800000000000)
#define USER_SPACE_END ((void*)0x00007FFFFFFFFFFF)

static void tlb_shootdown_ipi(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	tlb_flush_range(atomic_load(&shootdown_address, ATOMIC_ACQUIRE), atomic_load(&shootdown_size, ATOMIC_ACQUIRE));
	atomic_sub_fetch(&shootdown_remaining, 1, ATOMIC_RELEASE);
}

static void do_shootdown(struct cpu** cpus, u64 cpu_count, void* address, size_t size) {
	spinlock_lock(&shootdown_lock);

	atomic_store(&shootdown_address, address, ATOMIC_RELEASE);
	atomic_store(&shootdown_size, size, ATOMIC_RELEASE);
	atomic_store(&shootdown_remaining, cpu_count - 1, ATOMIC_RELEASE);

	for (u64 i = 0; i < cpu_count; i++) {
		if (cpus[i] == current_cpu())
			continue;
		apic_send_ipi(cpus[i], shootdown_isr, APIC_IPITARGET_TARGET, true);
	}

	while (atomic_load(&shootdown_remaining, ATOMIC_ACQUIRE))
		cpu_relax();

	spinlock_unlock(&shootdown_lock);
}

void tlb_invalidate(void* address, size_t size) {
	u64 cpu_count;
	struct cpu** cpus = get_cpu_structs(&cpu_count);

	unsigned long irq = local_irq_save();

	struct thread* current_thread = current_cpu()->runqueue.current;
	if (current_thread && cpu_count > 1 && 
			atomic_load(&current_thread->proc->thread_count, ATOMIC_ACQUIRE) > 1)
		do_shootdown(cpus, cpu_count, address, size);

	tlb_flush_range(address, size);
	local_irq_restore(irq);
}

void tlb_init(void) {
	shootdown_isr = interrupt_register(&shootdown_irq, tlb_shootdown_ipi);
	assert(shootdown_isr != NULL);
}
