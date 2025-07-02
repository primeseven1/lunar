#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/locking.h>
#include <crescent/core/term.h>
#include <crescent/lib/format.h>
#include <crescent/lib/string.h>

static char printk_buf[1024 + 1];
static spinlock_t printk_lock = SPINLOCK_INITIALIZER;
static void (*printk_hooks[5])(const struct printk_msg*) = { 0 };
static unsigned int printk_level = CONFIG_PRINTK_LEVEL;

int printk_set_hook(void (*hook)(const struct printk_msg*)) {
	unsigned long flags;
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
	unsigned long flags;
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

	unsigned long flags;
	spinlock_lock_irq_save(&printk_lock, &flags);
	printk_level = level;
	spinlock_unlock_irq_restore(&printk_lock, &flags);

	return 0;
}

const char* printk_level_string(unsigned int level) {
	switch (level) {
	case PRINTK_DBG_N:
		return "\033[32m[DBG]\033[0m ";
	case PRINTK_INFO_N:
		return "\033[97m[INFO]\033[0m ";
	case PRINTK_WARN_N:
		return "\033[33m[WARN]\033[0m ";
	case PRINTK_ERR_N:
		return "\033[31m[ERR]\033[0m ";
	case PRINTK_CRIT_N:
		return "\033[31m[CRIT]\033[0m ";
	case PRINTK_EMERG_N:
		return "\033[31m[EMERG]\033[0m ";
	}

	return NULL;
}

int vprintk(const char* fmt, va_list va) {
	unsigned long flags;
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

	/* clang-format off */
	struct printk_msg msg = {
		.msg = buf, .msg_level = level, .global_level = printk_level, .len = strlen(buf)
	};
	/* clang-format on */

	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i])
			printk_hooks[i](&msg);
	}

	if (level <= printk_level) {
		const char* lvlstr = printk_level_string(level);
		term_write(lvlstr, strlen(lvlstr));
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
