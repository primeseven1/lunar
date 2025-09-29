#pragma once

#include <lunar/types.h>

typedef void (*softirq_handler_t)(void);

enum softirqs {
	SOFTIRQ_TIMER,
	SOFTIRQ_COUNT
};

void do_pending_softirqs(bool daemon);
int register_softirq(softirq_handler_t action, int type);
int raise_softirq(int num);

void softirq_cpu_init(void);
