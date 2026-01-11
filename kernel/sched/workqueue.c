#include <lunar/core/panic.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/cpu.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/wrap.h>
#include <lunar/lib/list.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/slab.h>
#include "internal.h"

static struct slab_cache* atomic_work_cache = NULL;

static LIST_HEAD_DEFINE(global_workqueue);
static SPINLOCK_DEFINE(global_lock);
static SEMAPHORE_DEFINE(global_sem, 0);

static int worker_thread(void* arg) {
	struct cpu* const cpu = arg;

	struct list_head* const queue = cpu ? &cpu->workqueue : &global_workqueue;
	spinlock_t* const lock = cpu ? &cpu->workqueue_lock : &global_lock;
	struct semaphore* const sem = cpu ? &cpu->workqueue_sem : &global_sem;

	while (1) {
		semaphore_wait(sem, 0);

		irqflags_t irq;
		spinlock_lock_irq_save(lock, &irq);

		struct work* work = NULL;
		if (!list_empty(queue)) {
			work = list_first_entry(queue, struct work, link);
			list_remove(&work->link);
		}

		spinlock_unlock_irq_restore(lock, &irq);
		if (work) {
			work->fn(work->arg);
			slab_cache_free(atomic_work_cache, work);
		}
	}

	kthread_exit(0);
}

static int __sched_workqueue_add(struct cpu* cpu, void (*fn)(void*), void* arg) {
	struct work* work = slab_cache_alloc(atomic_work_cache);
	if (!work)
		return -ENOMEM;

	work->fn = fn;
	work->arg = arg;
	list_node_init(&work->link);

	struct list_head* const queue = cpu ? &cpu->workqueue : &global_workqueue;
	spinlock_t* const lock = cpu ? &cpu->workqueue_lock : &global_lock;
	struct semaphore* const sem = cpu ? &cpu->workqueue_sem : &global_sem;

	irqflags_t irq_flags;
	spinlock_lock_irq_save(lock, &irq_flags);
	list_add_tail(queue, &work->link);
	spinlock_unlock_irq_restore(lock, &irq_flags);

	semaphore_signal(sem);
	return 0;
}

int sched_workqueue_add(void (*fn)(void*), void* arg) {
	return __sched_workqueue_add(NULL, fn, arg);
}

int sched_workqueue_add_on(struct cpu* cpu, void(*fn)(void*), void* arg) {
	return __sched_workqueue_add(cpu, fn, arg);
}

void workqueue_cpu_init(void) {
	struct cpu* cpu = current_cpu();

	semaphore_init(&cpu->workqueue_sem, 0);
	spinlock_init(&cpu->workqueue_lock);
	list_head_init(&cpu->workqueue);

	u32 sched_id = cpu->sched_processor_id;
	int tflags = TOPOLOGY_THIS_CPU | TOPOLOGY_NO_MIGRATE;
	struct thread* global = kthread_create(tflags, worker_thread, NULL, "worker/%u:g", sched_id);
	struct thread* percpu = kthread_create(tflags, worker_thread, cpu, "worker/%u:p", sched_id);
	if (unlikely(!global || !percpu))
		panic("Failed to create workqueue threads");

	int g_err = kthread_run(global, SCHED_PRIO_DEFAULT);
	int p_err = kthread_run(percpu, SCHED_PRIO_DEFAULT);
	if (unlikely(g_err || p_err))
		panic("Failed to run workqueue threads (global: %i, percpu: %i)", g_err, p_err);

	kthread_detach(global);
	kthread_detach(percpu);
}

void workqueue_init(void) {
	atomic_work_cache = slab_cache_create(sizeof(struct work), _Alignof(struct work),
			MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (unlikely(!atomic_work_cache))
		panic("Failed to create atomic workqueue cache");

	workqueue_cpu_init();
}
