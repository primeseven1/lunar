#pragma once

typedef void (*softirq_handler_t)(void);

enum softirqs {
	SOFTIRQ_TIMER,
	SOFTIRQ_COUNT
};

void do_pending_softirqs(void);
int register_softirq(softirq_handler_t action, int type);
int raise_softirq(int num);
