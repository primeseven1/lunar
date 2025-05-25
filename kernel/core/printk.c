#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/locking.h>
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

void printk_set_level(unsigned int level) {
	unsigned long flags;
	spinlock_lock_irq_save(&printk_lock, &flags);
	if (level > PRINTK_DBG_N)
		level = PRINTK_DBG_N;
	printk_level = level;
	spinlock_unlock_irq_restore(&printk_lock, &flags);
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
			return len;
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
