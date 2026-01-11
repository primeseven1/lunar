#include "lunar/sched/scheduler.h"
#include <lunar/core/softirq.h>
#include <lunar/sched/preempt.h>
#include <lunar/core/printk.h>
#include <lunar/core/cpu.h>

static softirq_handler_t softirq_vec[SOFTIRQ_COUNT] = { NULL };
#define SOFTIRQ_PRIO 75

int register_softirq(softirq_handler_t action, int type) {
	if (type < 0 || type >= SOFTIRQ_COUNT)
		return -EINVAL;
	if (softirq_vec[type])
		return -EBUSY;

	softirq_vec[type] = action;
	return 0;
}

int raise_softirq(int num) {
	if (num < 0 || num >= SOFTIRQ_COUNT)
		return -EINVAL;

	struct cpu* cpu = current_cpu();
	cpu->softirqs_pending |= (1ul << num);

	return 0;
}

void do_pending_softirqs(bool daemon) {
	time_t max_ns = daemon ? 5000000 : 1000000;

	struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
	time_t start_ns = timespec_to_ns(&ts);

	int reent = 10;
	struct cpu* cpu = current_cpu();
	while (cpu->softirqs_pending) {
		unsigned long pending = cpu->softirqs_pending;
		cpu->softirqs_pending = 0; /* Softirq's may raise other softirq's */

		for (int i = 0; i < SOFTIRQ_COUNT; i++) {
			if (pending & (1 << i)) {
				if (softirq_vec[i])
					softirq_vec[i]();
			}

			ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
			if (timespec_to_ns(&ts) - start_ns >= max_ns)
				break;
		}

		if (reent-- == 0)
			break;
	}
}

static int softirq_daemon(void* arg) {
	(void)arg;

	int err = sched_change_prio(current_thread(), SOFTIRQ_PRIO);
	if (err) {
		printk(PRINTK_WARN "softirqd-%u: Failed to set priority: %i\n",
				current_cpu()->sched_processor_id, err);
	}

	while (1) {
		preempt_offset(SOFTIRQ_OFFSET);
		do_pending_softirqs(true);
		preempt_offset(-SOFTIRQ_OFFSET);
		schedule();
	}

	kthread_exit(0);
}

void softirq_cpu_init(void) {
	u32 sched_id = current_cpu()->sched_processor_id;
	struct thread* softirqd = kthread_create(TOPOLOGY_THIS_CPU | TOPOLOGY_NO_MIGRATE,
			softirq_daemon, NULL, "softirqd/%u", sched_id);
	if (unlikely(!softirqd))
		panic("Failed to create softirq daemon for CPU %u", sched_id);
	int err = kthread_run(softirqd, SCHED_PRIO_DEFAULT);
	if (err)
		panic("Failed to run softirq daemon for CPU %u", sched_id);
	kthread_detach(softirqd);
}
