#pragma once

#include <lunar/core/spinlock.h>
#include <lunar/lib/list.h>

struct thread;
struct cpu;

struct topology {
	struct cpu* target;
	struct list_head cpus;
	int flags;
};

enum topology_flags {
	TOPOLOGY_NO_MIGRATE = (1 << 0),
	TOPOLOGY_BSP = (1 << 1),
	TOPOLOGY_THIS_CPU = (1 << 2)
};

int topology_thread_init(struct thread* thread, int flags);
void topology_thread_destroy(struct thread* thread);
int topology_pick_cpu(struct thread* thread);
