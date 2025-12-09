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

int vprintk(const char* fmt, va_list va) {
	struct printk_msg msg;

	int len = vsnprintf(msg.msg, sizeof(msg.msg), fmt, va);
	if (len < 0)
		return len;
	msg.global_level = atomic_load(&printk_level);
	msg.level_count = 0;
	int level = PRINTK_INFO_N;
	if (msg.msg[0] == '\001') {
		level = msg.msg[1];
		if (level == '\0')
			return -1;
		msg.level_count = 2;
	}
	msg.level = level;
	if (msg.level > PRINTK_MAX_N)
		msg.level = PRINTK_MAX_N;

	printk_add_to_ringbuffer(&msg);
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
