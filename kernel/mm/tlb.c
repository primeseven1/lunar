#include <crescent/asm/wrap.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/cpu.h>
#include <crescent/core/apic.h>
#include <crescent/sched/kthread.h>
#include <crescent/init/status.h>
#include "internal.h"

static atomic(void*) shootdown_address;
static atomic(size_t) shootdown_size;
static atomic(u64) shootdown_remaining = atomic_init(0);
static SPINLOCK_DEFINE(shootdown_lock);

static struct isr* shootdown_isr;
static struct irq shootdown_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

#define KERNEL_SPACE_START ((void*)0xFFFF800000000000)
#define USER_SPACE_END ((void*)0x00007FFFFFFFFFFF)

static void shootdown_ipi(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	tlb_flush_range(atomic_load(&shootdown_address), atomic_load(&shootdown_size));
	atomic_sub_fetch(&shootdown_remaining, 1);
}

static void do_shootdown(const struct smp_cpus* cpus, void* address, size_t size) {
	spinlock_lock(&shootdown_lock);

	atomic_store(&shootdown_address, address);
	atomic_store(&shootdown_size, size);
	atomic_store(&shootdown_remaining, cpus->count - 1);

	for (u64 i = 0; i < cpus->count; i++) {
		if (cpus->cpus[i] == current_cpu())
			continue;
		bug(apic_send_ipi(cpus->cpus[i], shootdown_isr, APIC_IPI_CPU_TARGET, true) != 0);
	}

	while (atomic_load(&shootdown_remaining))
		cpu_relax();

	spinlock_unlock(&shootdown_lock);
}

void tlb_invalidate(void* address, size_t size) {
	const struct smp_cpus* cpus = smp_cpus_get();
	unsigned long irq = local_irq_save();

	if (likely(init_status_get() >= INIT_STATUS_SCHED)) {
		struct thread* current = current_thread();
		int thread_count = atomic_load(&current->proc->thread_count);
		if (cpus->count > 1 && thread_count > 1)
			do_shootdown(cpus, address, size);
	}

	tlb_flush_range(address, size);
	local_irq_restore(irq);
}

void vmm_tlb_init(void) {
	u32 count = smp_cpus_get()->count;
	if (count == 1)
		return;

	shootdown_isr = interrupt_alloc();
	if (unlikely(!shootdown_isr))
		panic("Failed to create TLB shootdown ISR\n");

	interrupt_register(shootdown_isr, &shootdown_irq, shootdown_ipi);
}
