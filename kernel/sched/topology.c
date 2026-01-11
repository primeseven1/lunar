#include <lunar/sched/procthrd.h>
#include <lunar/sched/scheduler.h>
#include <lunar/core/cpu.h>
#include <lunar/mm/slab.h>

struct topology_node {
	struct cpu* cpu;
	struct list_node link;
};

static struct slab_cache* topology_node_cache = NULL;

static struct topology_node* topology_create_node(struct cpu* cpu) {
	if (unlikely(!topology_node_cache)) {
		topology_node_cache = slab_cache_create(sizeof(struct topology_node),
				_Alignof(struct topology_node), MM_ZONE_NORMAL, NULL, NULL);
		if (unlikely(!topology_node_cache))
			panic("Failed to create topology node cache");
	}

	struct topology_node* node = slab_cache_alloc(topology_node_cache);
	if (!node)
		return NULL;

	node->cpu = cpu;
	list_node_init(&node->link);

	return node;
}

static inline void topology_destroy_node(struct topology_node* node) {
	slab_cache_free(topology_node_cache, node);
}

static int topology_add_node(struct topology* topology, struct cpu* cpu) {
	struct topology_node* node = topology_create_node(cpu);
	if (!node)
		return -ENOMEM;

	list_add(&topology->cpus, &node->link);
	return 0;
}

static inline void topology_remove_node(struct topology_node* node) {
	list_remove(&node->link);
	topology_destroy_node(node);
}

int topology_thread_init(struct thread* thread, int flags) {
	thread->topology.target = NULL;
	list_head_init(&thread->topology.cpus);

	if (flags & TOPOLOGY_THIS_CPU)
		return flags & TOPOLOGY_BSP ? -EINVAL : topology_add_node(&thread->topology, current_cpu());
	const struct smp_cpus* cpus = smp_cpus_get();
	if (flags & TOPOLOGY_BSP)
		return topology_add_node(&thread->topology, cpus->cpus[0]);

	for (u32 i = 0; i < cpus->count; i++) {
		int err = topology_add_node(&thread->topology, cpus->cpus[i]);
		if (err) {
			topology_thread_destroy(thread);
			return err;
		}
	}

	thread->topology.flags = flags;
	return 0;
}

void topology_thread_destroy(struct thread* thread) {
	struct topology_node* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &thread->topology.cpus, link)
		topology_remove_node(pos);
}

int topology_pick_cpu(struct thread* thread) {
	if (thread->topology.target)
		return -EALREADY;

	unsigned long best_tc = ULONG_MAX;
	struct topology_node* best = NULL;

	struct list_node* n;
	list_for_each(n, &thread->topology.cpus) {
		struct topology_node* current = list_entry(n, struct topology_node, link);
		unsigned long current_tc = atomic_load(&current->cpu->runqueue.thread_count);
		if (current_tc < best_tc) {
			best = current;
			best_tc = current_tc;
		}
	}
	if (unlikely(!best))
		return -EINVAL;

	thread->topology.target = best->cpu;
	return 0;
}
