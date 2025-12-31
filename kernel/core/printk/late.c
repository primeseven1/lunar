#include <lunar/core/printk.h>
#include <lunar/core/abi.h>
#include <lunar/core/timekeeper.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/lib/string.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include "internal.h"

static struct ringbuffer printk_rb;
static SPINLOCK_DEFINE(printk_rb_lock);

static char* printk_read(struct printk_record* record) {
	const size_t size = PRINTK_MAX_LEN + PRINTK_TIME_LEN + 2;
	char* buf = kzalloc(size, MM_ZONE_NORMAL);
	if (!buf)
		return NULL;

	char* ret = buf;
	bool fail = false;

	irqflags_t irq_flags;
	spinlock_lock_irq_save(&printk_rb_lock, &irq_flags);

	if (ringbuffer_size(&printk_rb) < sizeof(*record)) {
		fail = true;
		goto out;
	}

	ringbuffer_read(&printk_rb, record, sizeof(*record));
	const size_t read_count = record->len > PRINTK_MAX_LEN ? PRINTK_MAX_LEN : record->len;
	const size_t rest = record->len - read_count;

	printk_msg_time(record->level, &record->timestamp, buf, size);
	const size_t timecnt = strlen(buf);
	bug(timecnt != PRINTK_TIME_LEN);
	buf += timecnt;

	ringbuffer_read(&printk_rb, buf, read_count);
	if (rest)
		ringbuffer_read(&printk_rb, NULL, rest);
out:
	spinlock_unlock_irq_restore(&printk_rb_lock, &irq_flags);
	if (fail) {
		kfree(ret);
		ret = NULL;
	}
	return ret;
}

static int dump_message(void) {
	struct printk_record record;
	char* msg = printk_read(&record);
	if (!msg)
		return -ENODATA;
	printk_call_hooks(record.level, msg);
	kfree(msg);
	return 0;
}

static int printk_kthread(void* arg) {
	(void)arg;
	while (1) {
		if (dump_message() != 0)
			schedule();
	}

	return 0;
}

void do_printk_late(int level, const char* msg) {
	struct printk_record hdr = {
		.level = level,
		.timestamp = timekeeper_time(TIMEKEEPER_FROMBOOT),
		.len = strlen(msg)
	};

	irqflags_t irq_flags;
	spinlock_lock_irq_save(&printk_rb_lock, &irq_flags);

	size_t need = sizeof(hdr) + hdr.len;
	while (ringbuffer_space(&printk_rb) < need) {
		struct printk_record old;
		ringbuffer_read(&printk_rb, &old, sizeof(old));
		ringbuffer_read(&printk_rb, NULL, old.len);
	}
	ringbuffer_write(&printk_rb, &hdr, sizeof(hdr));
	ringbuffer_write(&printk_rb, msg, hdr.len);

	spinlock_unlock_irq_restore(&printk_rb_lock, &irq_flags);
}

int printk_late_init(void) {
	int err = ringbuffer_init(&printk_rb, 131072);
	if (err)
		return err;
	tid_t id = kthread_create(SCHED_THIS_CPU, printk_kthread, NULL, "printk");
	if (id < 0)
		return -ESRCH;
	kthread_detach(id);
	return 0;
}

void printk_late_sched_gone(void) {
	spinlock_unlock(&printk_rb_lock);
	while (dump_message() == 0)
		/* Nothing */;
}
