#pragma once

#include <crescent/asm/errno.h>

typedef long long time_t;

#define __timekeeper __attribute__((section(".timekeepers"), aligned(8), used))

struct timekeeper_source {
	const char* name;
	int (*init)(void);
	time_t (*get_ticks)(void);
	unsigned long long freq;
	unsigned int rating;
};

time_t timekeeper_get_nsec(void);

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
void timekeeper_stall(unsigned long usec);

void timekeeper_init(void);
