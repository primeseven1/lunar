#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/lib/format.h>
#include <lunar/mm/slab.h>
#include "internal.h"

static atomic(int) printk_level = atomic_init(CONFIG_PRINTK_LEVEL);

int printk_set_level(int level) {
	if (level > PRINTK_MAX_N || level < 0)
		return -EINVAL;

	atomic_store(&printk_level, level);
	return 0;
}

static struct slab_cache* msg_cache;
static atomic(bool) use_early = atomic_init(true);

int vprintk(const char* fmt, va_list va) {
	static struct printk_msg static_msg;

	bool early = atomic_load(&use_early);
	struct printk_msg* msg = early ? &static_msg : slab_cache_alloc(msg_cache);
	if (!msg)
		return -1;

	int len = vsnprintf(msg->_msg, sizeof(msg->_msg), fmt, va);
	if (len < 0)
		goto err;
	msg->msg = msg->_msg;
	msg->global_level = atomic_load(&printk_level);
	msg->cache = early ? NULL : msg_cache;
	int level = PRINTK_INFO_N;
	if (msg->_msg[0] == '\001') {
		level = msg->_msg[1];
		if (level == '\0')
			goto err;
		msg->msg += 2;
	}
	msg->level = level;
	if (msg->level > PRINTK_MAX_N)
		msg->level = PRINTK_MAX_N;

	if (early)
		printk_handle_message_early(msg);
	else
		printk_add_to_ringbuffer(msg);

	return len;
err:
	if (!early)
		slab_cache_free(msg_cache, msg);
	return -1;
}

__attribute__((format(printf, 1, 2)))
int printk(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int ret = vprintk(fmt, va);
	va_end(va);
	return ret;
}

void printk_init(void) {
	msg_cache = slab_cache_create(sizeof(struct printk_msg), _Alignof(struct printk_msg), 
			MM_ATOMIC | MM_ZONE_NORMAL, NULL, NULL);
	if (!msg_cache)
		panic("printk_init() failed!");
	atomic_store(&use_early, false);
}

void printk_in_panic(void) {
	atomic_store(&use_early, true);
	__printk_in_panic();
}
