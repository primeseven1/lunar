#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/asm/wrap.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/timekeeper.h>
#include <crescent/core/interrupt.h>

extern struct timekeeper_source _ld_kernel_timekeepers_start[];
extern struct timekeeper_source _ld_kernel_timekeepers_end[];

static struct timekeeper_source* timekeeper;

static struct timekeeper_source* get_timekeeper(void) {
	struct timekeeper_source* start = _ld_kernel_timekeepers_start;
	struct timekeeper_source* end = _ld_kernel_timekeepers_end;

	unsigned long count = ((uintptr_t)end - (uintptr_t)start) / sizeof(struct timekeeper_source);
	struct timekeeper_source* best = NULL;
	for (unsigned long i = 0; i < count; i++) {
		if (start[i].rating == 0)
			continue;

		if (!best)
			best = &start[i];
		else if (start[i].rating > best->rating)
			best = &start[i];
	}

	return best;
}

static time_t timekeeper_get_ticks(void) {
	if (!timekeeper)
		return 0;
	return timekeeper->get_ticks();	
}

time_t timekeeper_get_nsec(void) {
	if (!timekeeper)
		return 0;

	time_t ticks = timekeeper_get_ticks();
	return (ticks * 1000000000ull) / timekeeper->freq;
}

void timekeeper_stall(unsigned long usec) {
	assert(timekeeper != NULL); /* Expecting a stall, so crash to indicate a programming error */
	assert(usec <= 5000000); /* More than 5 seconds is dumb, so crash here to catch a programming error */

	unsigned long irq_state = local_irq_save();

	unsigned long long ticks_per_us = timekeeper->freq / 1000000;
	time_t start = timekeeper_get_ticks();
	time_t end = start + (usec * ticks_per_us);

	while (timekeeper_get_ticks() < end)
		cpu_relax();

	local_irq_restore(irq_state);
}

void timekeeper_init(void) {
	struct timekeeper_source* selected;
	do {
		selected = get_timekeeper();
		if (selected->init() == 0)
			break;
		selected->rating = 0;
	} while (selected);

	assert(selected != NULL);
	timekeeper = selected;
	printk(PRINTK_INFO "core: Timekeeper source %s chosen (frequency %llu mhz)\n", 
			timekeeper->name, timekeeper->freq / 1000000);
}
