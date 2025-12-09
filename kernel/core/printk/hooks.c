#include <lunar/core/printk.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/term.h>
#include <lunar/lib/string.h>
#include "internal.h"

static SPINLOCK_DEFINE(printk_lock);
static void (*printk_hooks[5])(const struct printk_msg*) = { 0 };

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

void printk_call_hooks(const struct printk_msg* msg) {
	irqflags_t flags;
	spinlock_lock_irq_save(&printk_lock, &flags);

	for (size_t i = 0; i < ARRAY_SIZE(printk_hooks); i++) {
		if (printk_hooks[i])
			printk_hooks[i](msg);
	}

	spinlock_unlock_irq_restore(&printk_lock, &flags);

	if (msg->level <= msg->global_level) {
		term_write(msg->time, strlen(msg->time));
		const char* _msg = msg->msg + msg->level_count;
		term_write(_msg, strlen(_msg));
	}
}
