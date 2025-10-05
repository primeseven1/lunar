#include <lunar/common.h>
#include <lunar/core/printk.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/term.h>
#include <lunar/core/timekeeper.h>
#include <lunar/lib/format.h>
#include <lunar/lib/string.h>

static char printk_buf[1024 + 1];
static SPINLOCK_DEFINE(printk_lock);
static void (*printk_hooks[5])(const struct printk_msg*) = { 0 };
static unsigned int printk_level = CONFIG_PRINTK_LEVEL;

int printk_set_hook(void (*hook)(const struct printk_msg*)) {
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

int printk_remove_hook(void (*hook)(const struct printk_msg*)) {
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

int printk_set_level(unsigned int level) {
	if (level > PRINTK_DBG_N)
		return -EINVAL;

	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);
	printk_level = level;
	spinlock_unlock_irq_restore(&printk_lock, &flags);

	return 0;
}

static const char* printk_level_string(unsigned int level) {
	switch (level) {
	case PRINTK_DBG_N:
		return "\033[32m";
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

int vprintk(const char* fmt, va_list va) {
	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);

	char* buf = printk_buf;
	int len = vsnprintf(buf, sizeof(printk_buf), fmt, va);

	unsigned int level = PRINTK_INFO_N;
	if (buf[0] == '\001') {
		level = buf[1];
		if (level == '\0')
			goto out;
		buf += 2;
	}

	char time_string[50];
	const char* color = printk_level_string(level);
	struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
	snprintf(time_string, sizeof(time_string), "%s[%5llu.%06llu]\033[0m ", 
			color, ts.tv_sec, (time_t)ts.tv_nsec / 1000);

	/* clang-format off */
	struct printk_msg msg = {
		.msg = buf, .time = time_string, .msg_level = level, 
		.global_level = printk_level, .len = strlen(buf)
	};
	/* clang-format on */

	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i])
			printk_hooks[i](&msg);
	}

	if (level <= printk_level) {
		term_write(time_string, strlen(time_string));
		term_write(msg.msg, strlen(msg.msg));
	}
out:
	spinlock_unlock_irq_restore(&printk_lock, &flags);
	return len;
}

__attribute__((format(printf, 1, 2)))
int printk(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int ret = vprintk(fmt, va);
	va_end(va);
	return ret;
}

void printk_emerg_release_lock(void) {
	spinlock_unlock(&printk_lock);
}
