#include <lunar/core/panic.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/wrap.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/sched/kthread.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/cpu.h>
#include "internal.h"

static struct ringbuffer global_workqueue;
static SPINLOCK_DEFINE(global_lock);
static struct semaphore global_sem;

static int worker_thread(void* arg) {
	struct cpu* cpu = arg;

	spinlock_t* const lock = cpu ? &cpu->workqueue_lock : &global_lock;
	struct ringbuffer* const ringbuffer = cpu ? &cpu->workqueue : &global_workqueue;
	struct semaphore* const sem = cpu ? &cpu->workqueue_sem : &global_sem;
	while (1) {
		semaphore_wait(sem, 0);
		struct work work;

		irqflags_t irq;
		spinlock_lock_irq_save(lock, &irq);
		if (ringbuffer_dequeue(ringbuffer, &work) != 0) {
			spinlock_unlock_irq_restore(lock, &irq);
			continue;
		}
		spinlock_unlock_irq_restore(lock, &irq);
		work.fn(work.arg);
	}

	kthread_exit(0);
}

static int __sched_workqueue_add(struct ringbuffer* wq,
		struct semaphore* wq_sem, spinlock_t* wq_lock,
		void (*fn)(void*), void* arg) {
	int ret = 0;
	struct work work = { .fn = fn, .arg = arg };

	irqflags_t irq;
	spinlock_lock_irq_save(wq_lock, &irq);

	if (ringbuffer_enqueue(wq, &work) != 0)
		ret = -EAGAIN;

	spinlock_unlock_irq_restore(wq_lock, &irq);

	if (ret == 0)
		semaphore_signal(wq_sem);
	return ret;
}

int sched_workqueue_add(void (*fn)(void*), void* arg) {
	return __sched_workqueue_add(&global_workqueue, &global_sem, &global_lock, fn, arg);
}

int sched_workqueue_add_on(struct cpu* cpu, void(*fn)(void*), void* arg) {
	return __sched_workqueue_add(&cpu->workqueue, &cpu->workqueue_sem, &cpu->workqueue_lock, fn, arg);
}

void workqueue_cpu_init(void) {
	struct cpu* cpu = current_cpu();
	semaphore_init(&cpu->workqueue_sem, 0);
	spinlock_init(&cpu->workqueue_lock);
	assert(ringbuffer_init(&cpu->workqueue, 64, sizeof(struct work)) == 0);

	tid_t id = kthread_create(SCHED_THIS_CPU, worker_thread, NULL, 
			"worker%u-%u", current_cpu()->sched_processor_id, 0);
	if (id < 0)
		panic("Failed to create worker threads");
	bug(kthread_detach(id) != 0);
	id = kthread_create(SCHED_THIS_CPU, worker_thread, current_cpu(),
			"worker%u-%u", current_cpu()->sched_processor_id, 1);
	if (id < 0)
		panic("Failed to create worker threads");
	bug(kthread_detach(id) != 0);
}

void workqueue_init(void) {
	assert(ringbuffer_init(&global_workqueue, 1024, sizeof(struct work)) == 0);
	semaphore_init(&global_sem, 0);
	workqueue_cpu_init();
}
