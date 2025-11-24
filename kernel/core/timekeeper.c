#include <lunar/types.h>
#include <lunar/common.h>
#include <lunar/asm/wrap.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/cpu.h>
#include <lunar/core/interrupt.h>

static inline time_t scale_ticks_to_ns(time_t ticks, unsigned long long freq) {
	i128 tmp = (i128)ticks * 1000000000ull;
	return tmp / freq;
}

static struct timekeeper_source* wallclock_source = NULL;

struct timespec timekeeper_time(int type) {
	struct timekeeper_source* tk_src;
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };

	irqflags_t irq = local_irq_save();

	switch (type) {
	case TIMEKEEPER_FROMBOOT: {
		tk_src = current_cpu()->timekeeper;
		time_t ticks = tk_src ? ticks = tk_src->fb_ticks() : 0;
		time_t nsec = ticks ? scale_ticks_to_ns(ticks, tk_src->fb_freq) : 0;
		ts.tv_sec = nsec / 1000000000ull;
		ts.tv_nsec = nsec % 1000000000ull;
		break;
	}
	case TIMEKEEPER_WALLCLOCK: {
		tk_src = wallclock_source;
		if (tk_src)
			ts = tk_src->wc_get(tk_src);
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
	struct timekeeper_source* tk_src = current_cpu()->timekeeper;

	/*
	 * Returning instead of a panic can be bad since a stall is required for some hardware,
	 * and stalling for more than 5 seconds is dumb
	 */
	bug(tk_src == NULL);
	bug(usec > 5000000);

	unsigned long long ticks_per_us = tk_src->fb_freq / 1000000;
	time_t start = tk_src->fb_ticks();
	time_t end = start + (usec * ticks_per_us);

	while (tk_src->fb_ticks() < end)
		cpu_relax();

	local_irq_restore(irq_state);
}

extern struct timekeeper _ld_kernel_timekeepers_start[];
extern struct timekeeper _ld_kernel_timekeepers_end[];

static struct timekeeper* __get_timekeeper(int type, bool early) {
	struct timekeeper* best = NULL;
	size_t count = _ld_kernel_timekeepers_end - _ld_kernel_timekeepers_start;

	for (size_t i = 0; i < count; i++) {
		struct timekeeper* tk = &_ld_kernel_timekeepers_start[i];
		if (tk->type != type)
			continue;
		if (tk->rating == 0 || tk->early != early)
			continue;

		if (!best)
			best = tk;
		else if (tk->rating > best->rating)
			best = tk;
	}

	return best;
}

static struct timekeeper* get_timekeeper(int type, bool early, struct timekeeper_source** out_src) {
	struct timekeeper* keeper = __get_timekeeper(type, early);
	while (keeper) {
		if (keeper->init(out_src) == 0)
			break;
		keeper->rating = 0;
		keeper = __get_timekeeper(type, early);
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
	early_keeper = get_timekeeper(TIMEKEEPER_FROMBOOT, true, &source);
	if (!early_keeper)
		panic("No early timekeeper could be selected");
	current_cpu()->timekeeper = source;
	name = early_keeper->name;

	struct timekeeper* real = get_timekeeper(TIMEKEEPER_FROMBOOT, false, &source);
	if (real) {
		name = real->name;
		keeper = real;
		current_cpu()->timekeeper = source;
	} else {
		keeper = early_keeper;
	}

	get_timekeeper(TIMEKEEPER_WALLCLOCK, false, &wallclock_source);

	atomic_thread_fence(ATOMIC_RELEASE);
	printk(PRINTK_INFO "core: Timekeeper source %s chosen (frequency %llu mhz)\n", name, source->fb_freq / 1000000);
}
