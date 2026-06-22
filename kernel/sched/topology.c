#include <lunar/sched.h>
#include <lunar/smp.h>
#include "internal.h"

void sched_thread_topology_init(struct thread* thread, int flags) {
	atomic_store(&thread->topology.migratable, !(flags & SCHED_TOPOLOGY_NO_MIGRATE));
	flags &= (SCHED_TOPOLOGY_BSP | SCHED_TOPOLOGY_CURRENT);
	if (flags) {
		cpumask_memset(&thread->topology.cpumask, 0);
		if (flags & SCHED_TOPOLOGY_BSP)
			cpumask_set(&thread->topology.cpumask, 0, true);
		if (flags & SCHED_TOPOLOGY_CURRENT)
			cpumask_set(&thread->topology.cpumask, current_cpu()->runqueue.sched_id, true);
	} else {
		cpumask_memset(&thread->topology.cpumask, UINT_MAX);
	}
}

struct cpu* sched_topology_pick_cpu(struct thread* thread) {
	struct cpu* ret = current_cpu();

	if (likely(current_thread()))
		preempt_disable();
	struct smp_cpus smp_cpus;
	smp_cpus_read_acquire(&smp_cpus);

	for (u32 i = 0; i < smp_cpus.count; i++ ) {
		if (!cpumask_test(&thread->topology.cpumask, i))
			continue;
		struct cpu* cpu = smp_cpus.cpus[i];
		if (unlikely(!cpu))
			continue;
		if (atomic_load(&cpu->runqueue.thread_count) < atomic_load(&ret->runqueue.thread_count))
			ret = cpu;
	}

	smp_cpus_read_release(&smp_cpus);
	if (likely(current_thread()))
		preempt_enable();
	return ret;
}
