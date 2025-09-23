#include <lunar/core/term.h>
#include <lunar/core/module.h>
#include <lunar/core/cmdline.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/core/spinlock.h>
#include <lunar/mm/heap.h>
#include <lunar/lib/list.h>

struct term_hook {
	void (*write)(const char*, size_t);
	struct list_node link;
};

static LIST_HEAD_DEFINE(term_hook_head);
static SPINLOCK_DEFINE(hooks_lock);

int term_driver_register(void (*write)(const char*, size_t)) {
	struct term_hook* hook = kmalloc(sizeof(*hook), MM_ZONE_NORMAL);
	if (!hook)
		return -ENOMEM;
	list_node_init(&hook->link);
	hook->write = write;

	int ret = 0;

	unsigned long flags;
	spinlock_lock_irq_save(&hooks_lock, &flags);

	struct term_hook* pos;
	list_for_each_entry(pos, &term_hook_head, link) {
		if (pos->write == write) {
			ret = -EALREADY;
			goto out;
		}
	}

	list_add(&term_hook_head, &hook->link);

out:
	spinlock_unlock_irq_restore(&hooks_lock, &flags);
	if (ret)
		kfree(hook);
	return ret;
}

void term_write(const char* str, size_t count) {
	unsigned long flags;
	spinlock_lock_irq_save(&hooks_lock, &flags);

	struct list_node* pos;
	list_for_each(pos, &term_hook_head) {
		struct term_hook* hook = list_entry(pos, struct term_hook, link);
		hook->write(str, count);
	}

	spinlock_unlock_irq_restore(&hooks_lock, &flags);
}

void term_init(void) {
	const char* driver = cmdline_get("term_driver");
	if (!driver) {
		printk(PRINTK_WARN "core: No terminal driver specified\n");
		return;
	}

	int err = module_load(driver);
	if (err) {
		printk(PRINTK_ERR "core: Failed to load terminal driver %s, err: %i\n", driver, err);
		return;
	}
}
