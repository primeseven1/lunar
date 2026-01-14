#include <lunar/asm/wrap.h>
#include <lunar/mm/vmm.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/cpu.h>
#include <lunar/core/intctl.h>
#include <lunar/sched/preempt.h>
#include <lunar/sched/kthread.h>
#include <lunar/init/status.h>
#include "internal.h"

static atomic(uintptr_t) shootdown_address;
static atomic(size_t) shootdown_size;
static atomic(u64) shootdown_remaining = atomic_init(0);
static SPINLOCK_DEFINE(shootdown_lock);

static struct isr* shootdown_isr;

static void shootdown_ipi(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	tlb_flush_range(atomic_load(&shootdown_address), atomic_load(&shootdown_size));
	atomic_sub_fetch(&shootdown_remaining, 1);
}

static void do_shootdown(const struct smp_cpus* cpus, uintptr_t address, size_t size) {
	spinlock_lock_preempt_disable(&shootdown_lock);

	atomic_store(&shootdown_address, address);
	atomic_store(&shootdown_size, size);
	atomic_store(&shootdown_remaining, cpus->count - 1);

	for (u32 i = 0; i < cpus->count; i++) {
		if (likely(cpus->cpus[i] != current_cpu()))
			bug(intctl_send_ipi(cpus->cpus[i], shootdown_isr, 0) != 0);
	}
	while (atomic_load(&shootdown_remaining))
		cpu_relax();

	spinlock_unlock_preempt_enable(&shootdown_lock);
}

void tlb_invalidate(uintptr_t address, size_t size) {
	const struct smp_cpus* cpus = smp_cpus_get();

	if (likely(init_status_get() >= INIT_STATUS_SCHED)) {
		struct thread* current = current_thread();
		int thread_count = atomic_load(&current->proc->thread_count);
		if (cpus->count > 1 && (address >= KERNEL_SPACE_START || thread_count > 1))
			do_shootdown(cpus, address, size);
	}

	tlb_flush_range(address, size);
}

void vmm_tlb_init(void) {
	u32 count = smp_cpus_get()->count;
	if (count == 1)
		return;

	shootdown_isr = interrupt_alloc();
	if (unlikely(!shootdown_isr))
		panic("Failed to create TLB shootdown ISR\n");

	interrupt_register(shootdown_isr, NULL, shootdown_ipi);
}
