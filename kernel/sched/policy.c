#include <lunar/string.h>
#include <lunar/printk.h>
#include <lunar/cmdline.h>
#include <lunar/panic.h>
#include <lunar/percpu.h>
#include <lunar/sched_policy.h>
#include "internal.h"

extern const struct sched_policy _ld_kernel_schedpolicies_start[];
extern const struct sched_policy _ld_kernel_schedpolicies_end[];

static inline void list_policies(void) {
	printk(PRINTK_CRIT "scheduling policy list:\n");
	size_t number = 1;
	for (const struct sched_policy* pol = _ld_kernel_schedpolicies_start; pol < _ld_kernel_schedpolicies_end; pol++, number++)
		printk(PRINTK_CRIT " %zu. %s (%s)\n", number, pol->desc, pol->name);
}

static const struct sched_policy* get_sched_policy_by_name(const char* name) {
	const struct sched_policy* it = _ld_kernel_schedpolicies_start;
	for (; it < _ld_kernel_schedpolicies_end; it++) {
		if (strcmp(it->name, name) == 0)
			return it;
	}

	return NULL;
}

#define POLICY_DEFAULT "pbrr"

static const struct sched_policy* decide_sched_policy(void) {
	const char* cmdline_name = cmdline_get("sched_policy");
	if (!cmdline_name)
		return get_sched_policy_by_name(POLICY_DEFAULT);

	const struct sched_policy* policy = get_sched_policy_by_name(cmdline_name);
	if (policy)
		return policy;

	printk(PRINTK_ERR "sched: sched_policy option %s not found\n", cmdline_name);
	list_policies();
	return get_sched_policy_by_name(POLICY_DEFAULT);
}

static inline void set_policy(const struct sched_policy* policy) {
	struct cpu* cpu = current_cpu();
	if (unlikely(policy->ops->init(&cpu->runqueue)))
		panic("Failed to set scheduling policy %s, please select a different policy", policy->name);
	cpu->runqueue.policy = policy;
}

void sched_policy_cpu_init(void) {
	static atomic(const struct sched_policy*) policy = atomic_init(NULL);
	if (atomic_load(&policy)) {
		set_policy(atomic_load(&policy));
		return;
	}

	atomic_store(&policy, decide_sched_policy());
	if (!atomic_load(&policy)) {
		list_policies();
		panic("No scheduling policy selected, try setting the sched_policy command line option");
	}

	set_policy(atomic_load(&policy));
	printk(PRINTK_INFO "sched: Using policy \"%s\"\n", atomic_load(&policy)->name);
}
