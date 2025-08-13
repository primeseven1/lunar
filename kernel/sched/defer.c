#include <crescent/core/panic.h>
#include <crescent/asm/cpuid.h>
#include <crescent/asm/wrap.h>
#include <crescent/lib/ringbuffer.h>
#include <crescent/sched/kthread.h>
#include <crescent/core/semaphore.h>
#include <crescent/core/cpu.h>
#include "sched.h"

static struct ringbuffer deferred_ringbuffer;
static spinlock_t deferred_lock = SPINLOCK_INITIALIZER;
static struct semaphore deferred_sem;

static void* worker_thread(void* arg) {
	struct cpu* cpu = arg;

	spinlock_t* const lock = cpu ? &cpu->deferred_lock : &deferred_lock;
	struct ringbuffer* const ringbuffer = cpu ? &cpu->deferred_ringbuffer : &deferred_ringbuffer;
	struct semaphore* const sem = cpu ? &cpu->deferred_sem : &deferred_sem;
	while (1) {
		semaphore_wait(sem);
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

int schedule_work(void (*fn)(void*), void* arg, int flags) {
	int ret = 0;

	unsigned long irq = local_irq_save();

	struct cpu* cpu = NULL;
	struct semaphore* sem = &deferred_sem;
	struct ringbuffer* ringbuffer = &deferred_ringbuffer;
	spinlock_t* lock = &deferred_lock;
	if (flags & SCHED_THIS_CPU) {
		cpu = current_cpu();
	} else if (flags & SCHED_CPU0) {
		u64 count;
		struct cpu** cpus = get_cpu_structs(&count);
		for (u64 i = 0; i < count; i++) {
			if (cpus[i]->sched_processor_id == 0)
				cpu = cpus[i];
		}
	}

	if (cpu) {
		sem = &cpu->deferred_sem;
		ringbuffer = &cpu->deferred_ringbuffer;
		lock = &cpu->deferred_lock;
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

void deferred_init_cpu(void) {
	struct cpu* cpu = current_cpu();
	semaphore_init(&cpu->deferred_sem, 0);
	spinlock_init(&cpu->deferred_lock);
	assert(ringbuffer_init(&cpu->deferred_ringbuffer, 32, sizeof(struct work)) == 0);

	struct thread* thread = kthread_create(SCHED_THIS_CPU, worker_thread, NULL);
	assert(thread != NULL);
	kthread_detach(thread);
	thread = kthread_create(SCHED_THIS_CPU, worker_thread, current_cpu());
	assert(thread != NULL);
	kthread_detach(thread);
}

void deferred_init(void) {
	assert(ringbuffer_init(&deferred_ringbuffer, 512, sizeof(struct work)) == 0);
	semaphore_init(&deferred_sem, 0);
	deferred_init_cpu();
}
