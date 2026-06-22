#include <lunar/printk.h>
#include <lunar/spinlock.h>
#include <lunar/string.h>
#include <lunar/format.h>
#include <lunar/cmdline.h>
#include <lunar/convert.h>
#include <lunar/timekeeper.h>
#include <lunar/ringbuffer.h>
#include <lunar/printk.h>
#include <lunar/panic.h>
#include <lunar/slab.h>
#include <lunar/init.h>
#include <lunar/term.h>
#include <lunar/kthread.h>
#include <lunar/sched.h>

#include <arch/irq_flags.h>

#define PRINTK_MAX_LEN 256
#define PRINTK_TIME_LEN 24

struct printk_record {
	int level;
	struct timespec timestamp;
	size_t len;
};

static struct ringbuffer printk_rb;
static SPINLOCK_DEFINE(printk_rb_lock);
static SEMAPHORE_DEFINE(printk_rb_sem, 0);

static void add_message_to_ringbuffer(int level, const char* msg) {
	struct printk_record hdr = { .level = level, .timestamp = time_fromboot(), .len = strlen(msg) };

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&printk_rb_lock, &irq_flags);

	size_t need = sizeof(hdr) + hdr.len;
	while (ringbuffer_freespace(&printk_rb) < need) {
		struct printk_record old;
		ringbuffer_read(&printk_rb, &old, sizeof(old));
		ringbuffer_read(&printk_rb, NULL, old.len);
	}
	ringbuffer_write(&printk_rb, &hdr, sizeof(hdr));
	ringbuffer_write(&printk_rb, msg, hdr.len);

	spinlock_release_irq_restore(&printk_rb_lock, &irq_flags);
}

static atomic(int) current_level = atomic_init(CONFIG_PRINTK_LEVEL);

void printk_set_level(int level) {
	if (level >= 0 && level < PRINTK_MAX_N)
		atomic_store_explicit(&current_level, level, ATOMIC_RELAXED);
}

static const char* printk_level_color_string(int level) {
	switch (level) {
	case PRINTK_INFO_N:
		return "\033[97m";
	case PRINTK_WARN_N:
		return "\033[33m";
	case PRINTK_ERR_N:
	case PRINTK_CRIT_N:
	case PRINTK_EMERG_N:
		return "\033[31m";
	case PRINTK_DBG_N:
	default:
		return "\033[36m";
	}
}

static void (*printk_hook)(int, int, const char*) = NULL;
static SPINLOCK_DEFINE(printk_hook_lock);

static void print_message(int level, const char* msg) {
	int global = atomic_load_explicit(&current_level, ATOMIC_RELAXED);

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&printk_hook_lock, &irq_flags);
	if (printk_hook)
		printk_hook(level, global, msg);
	spinlock_release_irq_restore(&printk_hook_lock, &irq_flags);

	if (level <= atomic_load_explicit(&current_level, ATOMIC_RELAXED))
		term_write(msg, strlen(msg));
}

static inline void printk_format_time(int level, struct timespec* ts, char* buf, size_t bufsize) {
	const char* color = printk_level_color_string(level);
	long usec = ts->tv_nsec / 1000;
	snprintf(buf, bufsize, "%s[%5lld.%06ld]\033[0m ", color, (long long)ts->tv_sec, usec);
}

static char* printk_read(struct printk_record* record) {
	const size_t size = PRINTK_MAX_LEN + PRINTK_TIME_LEN + 2;
	char* buf = kzalloc(size, MM_ZONE_NORMAL);
	if (!buf)
		return NULL;

	char* ret = buf;
	bool fail = false;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&printk_rb_lock, &irq_flags);

	if (ringbuffer_datacount(&printk_rb) < sizeof(*record)) {
		fail = true;
		goto out;
	}

	ringbuffer_read(&printk_rb, record, sizeof(*record));
	const size_t read_count = record->len > PRINTK_MAX_LEN ? PRINTK_MAX_LEN : record->len;
	const size_t rest = record->len - read_count;

	printk_format_time(record->level, &record->timestamp, buf, size);
	const size_t timecnt = strlen(buf);
	bug(timecnt != PRINTK_TIME_LEN);
	buf += timecnt;

	ringbuffer_read(&printk_rb, buf, read_count);
	if (rest)
		ringbuffer_read(&printk_rb, NULL, rest);
out:
	spinlock_release_irq_restore(&printk_rb_lock, &irq_flags);
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
	print_message(record.level, msg);
	kfree(msg);
	return 0;
}

/* TODO: Have this function sleep instead of just spinning until a new message shows up */
static int printk_kthread(void* arg) {
	(void)arg;
	while (1) {
		semaphore_wait(&printk_rb_sem, 0);
		dump_message();
	}

	return 0;
}

int printk_set_hook(void (*hook)(int, int, const char*)) {
	int err = 0;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&printk_hook_lock, &irq_flags);
	if (!printk_hook)
		printk_hook = hook;
	else
		err = -EEXIST;
	spinlock_release_irq_restore(&printk_hook_lock, &irq_flags);

	return err;
}

static atomic(bool) late = atomic_init(false);
static SPINLOCK_DEFINE(early_lock);

void printk_disable_ringbuffer_and_flush(void) {
	spinlock_release(&early_lock);
	spinlock_release(&printk_rb_lock);
	if (atomic_exchange(&late, false) == true) {
		while (dump_message() == 0)
			/* Nothing */;
	}
}

int vprintk(const char* fmt, va_list va) {
	char buf_array[PRINTK_MAX_LEN + 1];
	char* buf = buf_array;

	int len = vsnprintf(buf_array, sizeof(buf_array), fmt, va);
	if (len < 0)
		return len;

	int level = PRINTK_INFO_N;
	if (buf_array[0] == '\001') {
		level = buf_array[1];
		if (level == '\0')
			return -1;
		buf += 2;
	}
	if (level > PRINTK_MAX_N)
		level = PRINTK_MAX_N;

	if (likely(atomic_load(&late))) {
		add_message_to_ringbuffer(level, buf);
		semaphore_signal(&printk_rb_sem);
	} else {
		char timebuf[PRINTK_TIME_LEN + 1];
		struct timespec ts = time_fromboot();
		printk_format_time(level, &ts, timebuf, sizeof(timebuf));

		unsigned long irq_flags;
		spinlock_acquire_irq_save(&early_lock, &irq_flags);
		print_message(level, timebuf);
		print_message(level, buf);
		spinlock_release_irq_restore(&early_lock, &irq_flags);
	}

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

static void printk_init(void) {
	const char* loglevel_cmdline = cmdline_get("loglevel");

	unsigned long long loglevel;
	int err = kstrtoull(loglevel_cmdline, 0, &loglevel);
	if (err == 0) {
		if (loglevel > PRINTK_MAX_N)
			printk(PRINTK_ERR "printk: Invalid loglevel %llu\n", loglevel);
		else
			printk_set_level(loglevel);
		return;
	} else if (err == -ERANGE) {
		printk_set_level(PRINTK_ERR_N);
		printk(PRINTK_ERR "printk: loglevel %s, really?\n", loglevel_cmdline);
		printk_set_level(CONFIG_PRINTK_LEVEL);
		return;
	}

	static const struct {
		const char* string;
		int level;
	} level_strings[] = {
		{ .string = "DBG", .level = PRINTK_DBG_N }, { .string = "INFO", .level = PRINTK_INFO_N },
		{ .string = "WARN", .level = PRINTK_WARN_N }, { .string = "ERR", .level = PRINTK_ERR_N },
		{ .string = "CRIT", .level = PRINTK_CRIT_N }, { .string = "EMERG", .level = PRINTK_EMERG_N }
	};
	size_t i;
	for (i = 0; i < ARRAY_SIZE(level_strings); i++) {
		if (strcmp(level_strings[i].string, loglevel_cmdline) == 0) {
			printk_set_level(level_strings[i].level);
			break;
		}
	}
	if (i == ARRAY_SIZE(level_strings))
		printk(PRINTK_ERR "printk: Invalid loglevel %s\n", loglevel_cmdline);
}

INIT_TASK_DECLARE(cmdline_init_task, heap_init_task, sched_init_task);
INIT_TASK_DEFINE(printk_init_task, INIT_TASK_SCOPE_BSP, printk_init, &cmdline_init_task);

#define RBSIZE_DEFAULT 131072ul
#define RBSIZE_MAX 4194304ul

static void printk_late_init(void) {
	size_t rbsize = RBSIZE_DEFAULT;

	const char* logsize_cmdline = cmdline_get("logsize");
	if (logsize_cmdline) {
		unsigned long long cmdline_rbsize;
		int err = kstrtoull(logsize_cmdline, 0, &cmdline_rbsize);
		if (err == 0) {
			cmdline_rbsize = round_up_pow2(cmdline_rbsize);
			if (cmdline_rbsize >= RBSIZE_MAX) {
				printk(PRINTK_ERR "printk: logsize %llu is too big (maximum %lu)\n", cmdline_rbsize, RBSIZE_MAX);
				rbsize = RBSIZE_MAX;
			} else {
				rbsize = cmdline_rbsize;
			}
		}
	}

	int err = ringbuffer_init(&printk_rb, rbsize);
	if (err) {
		bug(err != -ENOMEM);
		printk("printk: ringbuffer_init() failed with -ENOMEM with size %zu\n", rbsize);
		err = ringbuffer_init(&printk_rb, RBSIZE_DEFAULT);
		if (err == -ENOMEM)
			out_of_memory();
		bug(err != 0);
	}

	struct thread* th = kthread_create(0, printk_kthread, NULL, "printk");
	if (!th)
		out_of_memory();
	err = kthread_run(th, SCHED_PRIO_DEFAULT);
	if (err)
		panic("Failed to run printk kthread");

	atomic_store(&late, true);
}

INIT_TASK_DEFINE(printk_late_init_task, INIT_TASK_SCOPE_BSP, printk_late_init,
		&printk_init_task, &heap_init_task, &sched_init_task);
