#include <lunar/irq.h>
#include <lunar/proc.h>
#include <lunar/vmm.h>
#include <arch/processor.h>
#include <arch/tlb.h>
#include "internal.h"

static atomic(struct isr*) shootdown_isr = atomic_init(NULL);
static atomic(uintptr_t) shootdown_address;
static atomic(size_t) shootdown_size;
static atomic(u32) shootdown_remaining = atomic_init(0);
static SPINLOCK_DEFINE(shootdown_lock);

static void shootdown_ipi(struct isr* isr) {
	(void)isr;
	arch_tlb_flush_range(atomic_load(&shootdown_address), atomic_load(&shootdown_size));
	atomic_sub_fetch(&shootdown_remaining, 1);
}

static void do_shootdown(uintptr_t address, size_t size) {
	spinlock_acquire_preempt_disable(&shootdown_lock);

	struct smp_cpus cpus;
	smp_cpus_read_acquire(&cpus);

	atomic_store(&shootdown_address, address);
	atomic_store(&shootdown_size, size);
	atomic_store(&shootdown_remaining, cpus.count - 1);

	for (u32 i = 0; i < cpus.count; i++) {
		if (cpus.cpus[i] != current_cpu())
			bug(irqctl_send_ipi(cpus.cpus[i], atomic_load(&shootdown_isr), 0) != 0);
	}
	while (atomic_load(&shootdown_remaining))
		arch_cpu_relax();

	smp_cpus_read_release(&cpus);
	spinlock_release_preempt_enable(&shootdown_lock);
}

void tlb_invalidate(uintptr_t address, size_t size) {
	arch_tlb_flush_range(address, size);
	if (atomic_load(&shootdown_isr)) {
		struct proc* proc = atomic_load(&current_thread()->proc);
		unsigned int proc_thread_count = atomic_load(&proc->threads.count);
		if (proc_thread_count > 1 || address >= KERNEL_SPACE_START)
			do_shootdown(address, size);
	}
}

void tlb_batch_init(struct tlb_batch* batch, pte_t* pagetable) {
	batch->pagetable = pagetable;
	batch->start = 0;
	batch->end = 0;
	batch->page_count = 0;
}

void tlb_batch_flush(struct tlb_batch* batch) {
	if (batch->start != batch->end)
		tlb_invalidate(batch->start, batch->end - batch->start);
	for (size_t i = 0; i < batch->page_count; i++)
		page_release(batch->pages[i]);

	batch->start = 0;
	batch->end = 0;
	batch->page_count = 0;
}

void tlb_batch_add(struct tlb_batch* batch, uintptr_t virtual, struct page* page) {
	if (batch->start == batch->end) {
		batch->start = virtual;
		batch->end = virtual + PAGE_SIZE;
	} else {
		if (virtual < batch->start)
			batch->start = virtual;
		if (virtual + PAGE_SIZE > batch->end)
			batch->end = virtual + PAGE_SIZE;
	}

	if (page) {
		if (batch->page_count == ARRAY_SIZE(batch->pages))
			tlb_batch_flush(batch);
		batch->pages[batch->page_count++] = page;
	}
}

void tlb_shootdown_init(void) {
	struct smp_cpus cpus;
	smp_cpus_read_acquire(&cpus);
	if (cpus.count > 1) {
		struct isr* isr = alloc_isr();
		if (!isr)
			out_of_memory();
		int err = register_isr(isr, shootdown_ipi, NULL, ISR_FLAG_TYPE_SGI);
		if (err)
			panic("Failed to register TLB shootdown");
		atomic_store(&shootdown_isr, isr);
	}
	smp_cpus_read_release(&cpus);
}
