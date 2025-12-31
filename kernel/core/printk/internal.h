#pragma once

#include <lunar/core/printk.h>
#include <lunar/core/abi.h>

#define PRINTK_MAX_LEN 256
#define PRINTK_TIME_LEN 24

struct printk_record {
	int level;
	struct timespec timestamp;
	size_t len;
};

void printk_msg_time(int level, struct timespec* ts, char* buf, size_t buf_size);
void printk_call_hooks(int level, const char* msg);
void do_printk_late(int level, const char* msg);
int printk_late_init(void);
void printk_late_sched_gone(void);
