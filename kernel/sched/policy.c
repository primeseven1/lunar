#include <lunar/lib/string.h>
#include <lunar/core/cmdline.h>
#include <lunar/core/printk.h>
#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include "internal.h"

extern const struct sched_policy _ld_kernel_schedpolicies_start[];
extern const struct sched_policy _ld_kernel_schedpolicies_end[];

static inline void list_policies(void) {
	uintptr_t dividend = (uintptr_t)_ld_kernel_schedpolicies_end - (uintptr_t)_ld_kernel_schedpolicies_start;
	size_t count = dividend / sizeof(struct sched_policy);

	printk(PRINTK_CRIT "scheduling policy list:\n");
	const struct sched_policy* policies = _ld_kernel_schedpolicies_start;
	for (size_t i = 0; i < count; i++)
		printk(PRINTK_CRIT " %zu. %s (%s)\n", i + 1, policies[i].desc, policies[i].name);
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
	static const struct sched_policy* policy = NULL;
	if (policy) {
		set_policy(policy);
		return;
	}

	policy = decide_sched_policy();
	if (!policy) {
		list_policies();
		panic("No scheduling policy selected, try setting the sched_policy command line option");
	}

	set_policy(policy);
	printk(PRINTK_INFO "sched: Using policy \"%s\"\n", policy->name);
}
