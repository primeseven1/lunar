#include <lunar/core/semaphore.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/sched/kthread.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/lib/format.h>
#include <lunar/mm/slab.h>
#include "internal.h"

static struct ringbuffer rb;
static SPINLOCK_DEFINE(time_lock);

static int printk_thread(void* _unused) {
	(void)_unused;

	while (1) {
		struct printk_msg* msg;
		if (ringbuffer_dequeue(&rb, &msg) != 0)
			continue;

		struct slab_cache* cache = msg->cache;
		msg->cache = NULL;
		printk_call_hooks(msg);

		bug(cache == NULL);
		slab_cache_free(cache, msg);
	}

	return 0;
}

static const char* printk_level_string(int level) {
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

static inline void handle_msg_time(struct printk_msg* msg) {
	const char* color = printk_level_string(msg->level);
	struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
	snprintf(msg->time, sizeof(msg->time), "%s[%5llu.%06llu]\033[0m ",
			color, ts.tv_sec, (time_t)ts.tv_nsec / 1000);
}

void printk_handle_message_early(struct printk_msg* msg) {
	handle_msg_time(msg);

	struct slab_cache* cache = msg->cache;
	msg->cache = NULL;
	printk_call_hooks(msg);

	if (cache)
		slab_cache_free(cache, msg);
}

static bool use_early = true;

void printk_rb_init(void) {
	if (unlikely(ringbuffer_init(&rb, RINGBUFFER_OVERWRITE, 1024, sizeof(struct printk_msg*))))
		return;
	tid_t id = kthread_create(SCHED_THIS_CPU, printk_thread, NULL, "printk");
	if (unlikely(id < 0)) {
		ringbuffer_destroy(&rb);
		return;
	}
	kthread_detach(id);
	use_early = false;
}

void printk_add_to_ringbuffer(struct printk_msg* msg) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&time_lock, &irq_flags);

	if (use_early) {
		printk_handle_message_early(msg);
	} else {
		handle_msg_time(msg);
		ringbuffer_enqueue(&rb, &msg);
	}

	spinlock_unlock_irq_restore(&time_lock, &irq_flags);
}

void __printk_sched_gone(void) {
	spinlock_unlock(&time_lock);
	use_early = true;
}
