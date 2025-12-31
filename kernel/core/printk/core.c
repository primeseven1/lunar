#include <lunar/common.h>
#include <lunar/core/printk.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/term.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/panic.h>
#include <lunar/lib/format.h>
#include <lunar/lib/string.h>
#include "internal.h"

static SPINLOCK_DEFINE(printk_lock);
static void (*printk_hooks[5])(int, int, const char*) = { 0 };

int printk_set_hook(void (*hook)(int, int, const char*)) {
	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);

	int ret = -ENOSPC;
	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i] == hook) {
			ret = -EALREADY;
			goto out;
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (!printk_hooks[i]) {
			printk_hooks[i] = hook;
			ret = 0;
			break;
		}
	}

out:
	spinlock_unlock_irq_restore(&printk_lock, &flags);
	return ret;
}

int printk_remove_hook(void (*hook)(int, int, const char*)) {
	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);

	int ret = -EFAULT;
	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i] == hook) {
			printk_hooks[i] = NULL;
			ret = 0;
			break;
		}
	}

	spinlock_unlock_irq_restore(&printk_lock, &flags);
	return ret;
}

static atomic(int) printk_level = atomic_init(CONFIG_PRINTK_LEVEL);

int printk_set_level(int level) {
	if (level > PRINTK_MAX_N || level < 0)
		return -EINVAL;
	atomic_store(&printk_level, level);
	return 0;
}

static const char* printk_level_color(int level) {
	switch (level) {
	case PRINTK_DBG_N:
		return "\033[36m";
	case PRINTK_INFO_N:
		return "\033[97m";
	case PRINTK_WARN_N:
		return "\033[33m";
	case PRINTK_ERR_N:
		return "\033[31m";
	case PRINTK_CRIT_N:
		return "\033[31m";
	case PRINTK_EMERG_N:
		return "\033[31m";
	}

	return NULL;
}

void printk_msg_time(int level, struct timespec* ts, char* buf, size_t buf_size) {
	const char* color = printk_level_color(level);
	time_t us = ts->tv_nsec / 1000;
	snprintf(buf, buf_size, "%s[%5llu.%06llu]\033[0m ", color, ts->tv_sec, us);
}

void printk_call_hooks(int level, const char* msg) {
	int global = atomic_load(&printk_level);

	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);
	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i])
			printk_hooks[i](level, global, msg);
	}
	spinlock_unlock_irq_restore(&printk_lock, &flags);

	if (level <= global)
		term_write(msg, strlen(msg));
}

static bool use_early = true;

void printk_init(void) {
	int err = printk_late_init();
	if (err)
		panic("printk_init() failed: %i", err);
	use_early = false;
}

int vprintk(const char* fmt, va_list va) {
	char buf[PRINTK_MAX_LEN + 1];
	char* _buf;

	int len = vsnprintf(buf, sizeof(buf), fmt, va);
	if (len < 0)
		return len;
	int level = PRINTK_INFO_N;
	if (buf[0] == '\001') {
		level = buf[1];
		if (level == '\0')
			return -1;
		_buf = buf + 2;
	} else {
		_buf = buf;
	}
	if (level > PRINTK_MAX_N)
		level = PRINTK_MAX_N;

	if (use_early) {
		char timebuf[PRINTK_TIME_LEN + 1];
		struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
		printk_msg_time(level, &ts, timebuf, sizeof(timebuf));
		/* No other threads, so do two calls */
		printk_call_hooks(level, timebuf);
		printk_call_hooks(level, _buf);
	} else {
		do_printk_late(level, _buf);
	}

	return 0;
}

__attribute__((format(printf, 1, 2)))
int printk(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int ret = vprintk(fmt, va);
	va_end(va);
	return ret;
}

void printk_sched_gone(void) {
	printk_late_sched_gone();
	use_early = true;
}
