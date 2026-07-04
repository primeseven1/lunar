#include <lunar/smp.h>
#include <lunar/sched.h>
#include "internal.h"

void topology_init(struct topology* topology, int flags) {
	atomic_store(&topology->cpu, NULL);
	atomic_store(&topology->migratable, !(flags & SCHED_TOPOLOGY_NO_MIGRATE));

	flags &= (SCHED_TOPOLOGY_BSP | SCHED_TOPOLOGY_CURRENT);
	if (flags) {
		cpumask_memset(&topology->cpumask, 0);
		if (flags & SCHED_TOPOLOGY_BSP)
			cpumask_set(&topology->cpumask, 0, true);
		if (flags & SCHED_TOPOLOGY_CURRENT)
			cpumask_set(&topology->cpumask, current_cpu()->runqueue.sched_id, true);
	} else {
		cpumask_memset(&topology->cpumask, UINT_MAX);
	}
}

struct cpu* topology_pick_cpu(struct topology* topology) {
	unsigned long irq_flags = local_irq_save();
	struct cpu* ret = NULL;

	struct smp_cpus smp_cpus;
	smp_cpus_read_acquire(&smp_cpus);

	for (u32 i = 0; i < smp_cpus.count; i++ ) {
		if (!cpumask_test(&topology->cpumask, i))
			continue;
		struct cpu* cpu = smp_cpus.cpus[i];
		if (unlikely(!cpu))
			continue;
		if (!ret || atomic_load(&cpu->runqueue.thread_count) < atomic_load(&ret->runqueue.thread_count))
			ret = cpu;
	}

	if (unlikely(!ret)) {
		struct cpu* current = current_cpu();
		/* This can happen in early init when creating bootstrap threads, softirq threads, etc. */
		if (cpumask_test(&topology->cpumask, current->runqueue.sched_id))
			ret = current;
	}

	smp_cpus_read_release(&smp_cpus);

	local_irq_restore(irq_flags);
	return ret;
}

int topology_set_cpu(struct topology* topology, struct cpu* cpu) {
	if (cpumask_test(&topology->cpumask, cpu->runqueue.sched_id) == false)
		return -EINVAL;
	atomic_store(&topology->cpu, cpu);
	return 0;
}
