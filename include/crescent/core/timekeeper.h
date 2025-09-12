#pragma once

#include <crescent/types.h>
#include <crescent/asm/errno.h>
#include <crescent/core/time.h>

#define __timekeeper __attribute__((section(".timekeepers"), aligned(8), used))

struct timekeeper;

struct timekeeper_source {
	time_t (*get_ticks)(void);
	unsigned long long freq;
	void* private;
};

struct timekeeper {
	const char* name;
	int (*init)(struct timekeeper_source**);
	int rating;
	bool early;
};

struct timespec timekeeper_time(void);

/**
 * @brief Stall the thread
 *
 * This function spins until `usec` has passed.
 * There will be no preemptions when this function is called,
 * and will not sleep.
 *
 * This function also crashes the kernel if stalling for more than 5 seconds
 * (since stalling for more than that is dumb), or when timekeeper_init hasn't
 * been called.
 *
 * @param usec The number of microseconds to stall for
 */
void timekeeper_stall(time_t usec);

void timekeeper_cpu_init(void);
void timekeeper_init(void);
