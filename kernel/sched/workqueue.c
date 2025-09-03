#include <crescent/core/panic.h>
#include <crescent/asm/cpuid.h>
#include <crescent/asm/wrap.h>
#include <crescent/lib/ringbuffer.h>
#include <crescent/sched/kthread.h>
#include <crescent/core/semaphore.h>
#include <crescent/core/cpu.h>
#include "internal.h"

static struct ringbuffer global_workqueue;
static SPINLOCK_DEFINE(global_lock);
static struct semaphore global_sem;

static void* worker_thread(void* arg) {
	struct cpu* cpu = arg;

	spinlock_t* const lock = cpu ? &cpu->workqueue_lock : &global_lock;
	struct ringbuffer* const ringbuffer = cpu ? &cpu->workqueue : &global_workqueue;
	struct semaphore* const sem = cpu ? &cpu->workqueue_sem : &global_sem;
	while (1) {
		semaphore_wait(sem, 0);
		struct work work;

		unsigned long irq;
		spinlock_lock_irq_save(lock, &irq);
		if (ringbuffer_dequeue(ringbuffer, &work) != 0) {
			spinlock_unlock_irq_restore(lock, &irq);
			continue;
		}
		spinlock_unlock_irq_restore(lock, &irq);
		work.fn(work.arg);
	}

	return NULL;
}

int sched_workqueue_add(void (*fn)(void*), void* arg, int flags) {
	int ret = 0;

	unsigned long irq = local_irq_save();

	struct cpu* cpu = sched_decide_cpu(flags);
	struct semaphore* sem = &global_sem;
	struct ringbuffer* ringbuffer = &global_workqueue;
	spinlock_t* lock = &global_lock;

	if (cpu) {
		sem = &cpu->workqueue_sem;
		ringbuffer = &cpu->workqueue;
		lock = &cpu->workqueue_lock;
	}

	spinlock_lock(lock);

	struct work work = {
		.fn = fn,
		.arg = arg,
	};

	if (ringbuffer_enqueue(ringbuffer, &work) == 0)
		semaphore_signal(sem);
	else
		ret = -EAGAIN;

	spinlock_unlock(lock);
	local_irq_restore(irq);
	return ret;
}

void workqueue_cpu_init(void) {
	struct cpu* cpu = current_cpu();
	semaphore_init(&cpu->workqueue_sem, 0);
	spinlock_init(&cpu->workqueue_lock);
	assert(ringbuffer_init(&cpu->workqueue, 64, sizeof(struct work)) == 0);

	struct thread* thread = kthread_create(SCHED_THIS_CPU, worker_thread, NULL);
	assert(thread != NULL);
	kthread_detach(thread);
	thread = kthread_create(SCHED_THIS_CPU, worker_thread, current_cpu());
	assert(thread != NULL);
	kthread_detach(thread);
}

void workqueue_init(void) {
	assert(ringbuffer_init(&global_workqueue, 1024, sizeof(struct work)) == 0);
	semaphore_init(&global_sem, 0);
	workqueue_cpu_init();
}
