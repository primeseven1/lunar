#include <crescent/core/term.h>
#include <crescent/core/module.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/core/locking.h>
#include <crescent/mm/heap.h>
#include <crescent/lib/list.h>

struct term_hook {
	void (*write)(const char*, size_t);
	struct list_node link;
};

static DEFINE_LIST_HEAD(term_hook_head);
static spinlock_t hooks_lock = SPINLOCK_INITIALIZER;

int term_driver_register(void (*write)(const char*, size_t)) {
	unsigned long flags;
	spinlock_lock_irq_save(&hooks_lock, &flags);

	int ret = 0;
	struct term_hook* hook;
	list_for_each_entry(hook, &term_hook_head, link) {
		if (hook->write == write) {
			ret = -EALREADY;
			goto out;
		}
	}

	hook = kmalloc(sizeof(*hook), MM_ZONE_NORMAL);
	if (!hook) {
		ret = -ENOMEM;
		goto out;
	}

	hook->write = write;
	list_node_init(&hook->link);
	list_add(&term_hook_head, &hook->link);
out:
	spinlock_unlock_irq_restore(&hooks_lock, &flags);
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
