#include <crescent/sched/kthread.h>
#include <crescent/core/cpu.h>
#include "internal.h"

static inline void reap_thread(struct runqueue* rq, struct thread* thread) {
	sched_thread_detach(rq, thread);
	thread_destroy(thread);
}

static int reaper_thread(void* arg) {
	(void)arg;

	unsigned long irq;
	struct runqueue* rq = &current_cpu()->runqueue;
	bool sleep = true;
	while (1) {
		if (sleep)
			semaphore_wait(&rq->reaper_sem, 0);

		struct thread* victim = NULL;
		spinlock_lock_irq_save(&rq->zombie_lock, &irq);
		sleep = list_empty(&rq->zombies);
		if (!sleep) {
			victim = list_first_entry(&rq->zombies, struct thread, zombie_link);
			list_remove(&victim->zombie_link);
			if (atomic_load(&victim->refcount, ATOMIC_ACQUIRE) > 0) {
				list_add_tail(&rq->zombies, &victim->zombie_link);
				victim = NULL;
			}
		}
		spinlock_unlock_irq_restore(&rq->zombie_lock, &irq);

		if (victim)
			reap_thread(rq, victim);
	}

	kthread_exit(0);
}

void reaper_cpu_init(void) {
	tid_t id = kthread_create(SCHED_THIS_CPU, reaper_thread, NULL, 
			"reaper-%u", current_cpu()->processor_id);
	if (id < 0)
		panic("Failed to create reaper thread(s)\n");
	bug(kthread_detach(id) != 0);
}
