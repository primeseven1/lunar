#include <lunar/types.h>
#include <lunar/common.h>
#include <lunar/asm/wrap.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/cpu.h>
#include <lunar/core/interrupt.h>

extern struct timekeeper _ld_kernel_timekeepers_start[];
extern struct timekeeper _ld_kernel_timekeepers_end[];

static struct timekeeper* get_timekeeper(int type, bool early) {
	struct timekeeper* start = _ld_kernel_timekeepers_start;
	struct timekeeper* end = _ld_kernel_timekeepers_end;

	unsigned long count = ((uintptr_t)end - (uintptr_t)start) / sizeof(*start);
	struct timekeeper* best = NULL;
	for (unsigned long i = 0; i < count; i++) {
		if (start[i].type != type)
			continue;
		if (start[i].rating == 0 || start[i].early != early)
			continue;

		if (!best)
			best = &start[i];
		else if (start[i].rating > best->rating)
			best = &start[i];
	}

	return best;
}

static inline time_t scale_ticks_to_ns(time_t ticks, unsigned long long freq) {
	i128 tmp = (i128)ticks * 1000000000ull;
	return tmp / freq;
}

static struct timekeeper_source* wallclock_source = NULL;

struct timespec timekeeper_time(int type) {
	irqflags_t irq = local_irq_save();

	struct timekeeper_source* timekeeper;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };

	switch (type) {
	case TIMEKEEPER_FROMBOOT: {
		timekeeper = current_cpu()->timekeeper;
		time_t ticks = timekeeper ? ticks = timekeeper->fb_ticks() : 0;
		time_t nsec = ticks ? scale_ticks_to_ns(ticks, timekeeper->fb_freq) : 0;
		ts.tv_sec = nsec / 1000000000ull;
		ts.tv_nsec = nsec % 1000000000ull;
		break;
	}
	case TIMEKEEPER_WALLCLOCK: {
		timekeeper = wallclock_source;
		ts = timekeeper->wc_get(timekeeper);
		break;
	}
	default:
		break;
	}

	local_irq_restore(irq);
	return ts;
}

void timekeeper_stall(time_t usec) {
	irqflags_t irq_state = local_irq_save();

	struct timekeeper_source* timekeeper = current_cpu()->timekeeper;
	bug(timekeeper == NULL); /* Trying to stall with no timekeeper when expecting a real stall can be bad */
	bug(usec > 5000000); /* More than 5 seconds is dumb */

	unsigned long long ticks_per_us = timekeeper->fb_freq / 1000000;
	time_t start = timekeeper->fb_ticks();
	time_t end = start + (usec * ticks_per_us);

	while (timekeeper->fb_ticks() < end)
		cpu_relax();

	local_irq_restore(irq_state);
}

static struct timekeeper* get_timekeeper_init(int type, bool early, struct timekeeper_source** out) {
	struct timekeeper* keeper = get_timekeeper(type, early);
	while (keeper) {
		if (keeper->init(out) == 0)
			break;
		keeper->rating = 0;
		keeper = get_timekeeper(0, early);
	}
	return keeper;
}

static struct timekeeper* early_keeper = NULL;
static struct timekeeper* keeper = NULL;

void timekeeper_cpu_init(void) {
	struct cpu* cpu = current_cpu();
	int err = early_keeper->init(&cpu->timekeeper);
	if (err)
		panic("Failed to initialize early timekeeper for AP");
	err = keeper->init(&current_cpu()->timekeeper);
	if (err)
		panic("Failed to initialize timekeeper for AP");
	printk(PRINTK_INFO "core: Timekeeper frequency %llu mhz on CPU %u\n", 
			cpu->timekeeper->fb_freq / 1000000, cpu->sched_processor_id);
}

void timekeeper_init(void) {
	const char* name = NULL;

	/* The real timekeeper may need another working timekeeper */
	struct timekeeper_source* source;
	early_keeper = get_timekeeper_init(TIMEKEEPER_FROMBOOT, true, &source);
	if (!early_keeper)
		panic("No early timekeeper could be selected");
	current_cpu()->timekeeper = source;
	name = early_keeper->name;

	/* Now initialize a real timekeeper (might just use the early one) */
	struct timekeeper* real = get_timekeeper_init(TIMEKEEPER_FROMBOOT, false, &source);
	if (real) {
		name = real->name;
		keeper = real;
		current_cpu()->timekeeper = source;
	} else {
		keeper = early_keeper;
	}

	if (!get_timekeeper_init(TIMEKEEPER_WALLCLOCK, false, &wallclock_source))
		panic("No wallclock source!\n");

	atomic_thread_fence(ATOMIC_SEQ_CST); /* keeper is read by other CPU's after this call, so do a fence here */
	printk(PRINTK_INFO "core: Timekeeper source %s chosen (frequency %llu mhz)\n", name, source->fb_freq / 1000000);
}
