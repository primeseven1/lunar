#include <lunar/core/softirq.h>
#include <lunar/sched/preempt.h>
#include <lunar/core/cpu.h>

static softirq_handler_t softirq_vec[SOFTIRQ_COUNT] = { NULL };

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

void do_pending_softirqs(void) {
	struct cpu* cpu = current_cpu();
	while (cpu->softirqs_pending) {
		unsigned long pending = cpu->softirqs_pending;
		cpu->softirqs_pending = 0; /* Softirq's may raise other softirq's */

		for (int i = 0; i < SOFTIRQ_COUNT; i++) {
			if (pending & (1 << i)) {
				if (softirq_vec[i])
					softirq_vec[i]();
			}
		}
	}
}
