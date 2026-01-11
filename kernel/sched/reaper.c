#include <lunar/asm/wrap.h>
#include <lunar/sched/kthread.h>
#include <lunar/init/status.h>
#include <lunar/core/cpu.h>
#include <lunar/core/printk.h>
#include "internal.h"

static inline void reap_thread(struct runqueue* rq, struct thread* thread) {
	sched_thread_detach(rq, thread);
	bug(thread_destroy(thread) != 0);
}

static int reaper_thread(void* arg) {
	(void)arg;

	irqflags_t irq;
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

			/* One ref for the process link, another for the attach */
			if (atomic_load(&victim->refcnt) > 2) {
				list_add_tail(&rq->zombies, &victim->zombie_link);
				victim = NULL;
			}
		}

		spinlock_unlock_irq_restore(&rq->zombie_lock, &irq);
		if (victim)
			reap_thread(rq, victim);
		else
			schedule();
	}

	kthread_exit(0);
}

void reaper_cpu_init(void) {
	struct thread* kt = kthread_create(TOPOLOGY_THIS_CPU | TOPOLOGY_NO_MIGRATE, reaper_thread, 
			NULL, "reaper/%u", current_cpu()->sched_processor_id);
	if (!kt)
		panic("Failed to create reaper thread(s)");
	int err = kthread_run(kt, SCHED_PRIO_DEFAULT);
	if (err)
		panic("Failed to run reaper thread(s)");
	kthread_detach(kt);
}
