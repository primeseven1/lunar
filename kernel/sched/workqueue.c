#include <lunar/core/panic.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/wrap.h>
#include <lunar/lib/list.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/slab.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/cpu.h>
#include "internal.h"

static LIST_HEAD_DEFINE(global_workqueue);
static SPINLOCK_DEFINE(global_lock);
static SEMAPHORE_DEFINE(global_sem, 0);

static int worker_thread(void* arg) {
	struct cpu* cpu = arg;

	spinlock_t* const lock = cpu ? &cpu->workqueue_lock : &global_lock;
	struct list_head* const list = cpu ? &cpu->workqueue : &global_workqueue;
	struct semaphore* const sem = cpu ? &cpu->workqueue_sem : &global_sem;
	while (1) {
		semaphore_wait(sem, 0);

		irqflags_t irq;
		spinlock_lock_irq_save(lock, &irq);

		struct work* work = NULL;
		if (!list_empty(list)) {
			work = list_first_entry(list, struct work, link);
			list_remove(&work->link);
		}

		spinlock_unlock_irq_restore(lock, &irq);
		if (work)
			work->fn(work->arg);
	}

	kthread_exit(0);
}

static struct slab_cache* atomic_work_cache = NULL;

static int __sched_workqueue_add(struct list_head* wq,
		struct semaphore* wq_sem, spinlock_t* wq_lock,
		void (*fn)(void*), void* arg) {
	struct work* work = slab_cache_alloc(atomic_work_cache);
	if (!work)
		return -ENOMEM;

	work->fn = fn;
	work->arg = arg;
	list_node_init(&work->link);

	irqflags_t irq;
	spinlock_lock_irq_save(wq_lock, &irq);
	list_add_tail(wq, &work->link);
	spinlock_unlock_irq_restore(wq_lock, &irq);

	semaphore_signal(wq_sem);
	return 0;
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
	list_head_init(&cpu->workqueue);

	/* global workqueue */
	tid_t id = kthread_create(SCHED_THIS_CPU, worker_thread, NULL, 
			"worker%u-%u", current_cpu()->sched_processor_id, 0);
	if (id < 0)
		panic("Failed to create worker threads");
	kthread_detach(id);

	/* per-cpu workqueue */
	id = kthread_create(SCHED_THIS_CPU, worker_thread, current_cpu(),
			"worker%u-%u", current_cpu()->sched_processor_id, 1);
	if (id < 0)
		panic("Failed to create worker threads");
	kthread_detach(id);
}

void workqueue_init(void) {
	atomic_work_cache = slab_cache_create(sizeof(struct work), _Alignof(struct work),
			MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (unlikely(!atomic_work_cache))
		panic("Failed to create atomic workqueue cache");
	workqueue_cpu_init();
}
