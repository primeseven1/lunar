#include <crescent/core/term.h>
#include <crescent/core/module.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/core/locking.h>
#include <crescent/mm/heap.h>

struct term_hook {
	void (*write)(const char*, size_t);
	struct term_hook* next;
};

static struct term_hook* term_hook_head = NULL;
static spinlock_t hooks_lock = SPINLOCK_STATIC_INITIALIZER;

static int __term_driver_register(void (*write)(const char*, size_t)) {
	if (!term_hook_head)
		return -EAGAIN;

	struct term_hook* hook = kmalloc(sizeof(*hook), MM_ZONE_NORMAL);
	if (!hook)
		return -ENOMEM;

	hook->write = write;
	hook->next = NULL;

	if (!term_hook_head->write) {
		term_hook_head->write = write;
		return 0;
	}

	struct term_hook* tail = term_hook_head;
	while (tail->next)
		tail = tail->next;
	tail->next = hook;

	return 0;
}

int term_driver_register(void (*write)(const char*, size_t)) {
	if (!write)
		return -EINVAL;

	unsigned long flags;
	spinlock_lock_irq_save(&hooks_lock, &flags);
	int ret = __term_driver_register(write);
	spinlock_unlock_irq_restore(&hooks_lock, &flags);

	return ret;
}

void term_write(const char* str, size_t count) {
	if (unlikely(!term_hook_head))
		return;

	unsigned long flags;
	spinlock_lock_irq_save(&hooks_lock, &flags);

	struct term_hook* hook = term_hook_head;
	while (hook) {
		if (likely(hook->write))
			hook->write(str, count);
		hook = hook->next;
	}

	spinlock_unlock_irq_restore(&hooks_lock, &flags);
}

void term_init(void) {
	term_hook_head = kzalloc(sizeof(*term_hook_head), MM_ZONE_NORMAL);
	assert(term_hook_head);

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
