#include <lunar/sched/scheduler.h>
#include <lunar/lib/hashtable.h>

static struct hashtable* process_table = NULL;

int sched_add_to_proctbl(struct proc* proc) {
	if (unlikely(!process_table)) {
		process_table = hashtable_create(64, sizeof(struct proc*));
		if (!process_table)
			panic("Failed to create process table!\n");
	}

	return hashtable_insert(process_table, &proc->pid, sizeof(proc->pid), &proc);
}

int sched_get_from_proctbl(pid_t pid, struct proc** proc) {
	return hashtable_search(process_table, &pid, sizeof(pid), proc);
}

int sched_remove_proctbl(pid_t pid) {
	return hashtable_remove(process_table, &pid, sizeof(pid));
}
