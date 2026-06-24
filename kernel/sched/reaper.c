#include <lunar/proc.h>
#include <lunar/kthread.h>

static int reaper(void* arg) {
	struct runqueue* rq = arg;
	while (1) {
		const int err = semaphore_wait(&rq->reaper_sem, 0);
		if (unlikely(err))
			continue;

		struct thread* zombie = NULL;
		spinlock_acquire_preempt_disable(&rq->zombie_lock);

		if (!list_empty(&rq->zombie_list)) {
			zombie = list_first_entry(&rq->zombie_list, struct thread, state.block_link);
			const unsigned long attached_refcnt = PROC_THREAD_ATTACHED_REFCOUNT + rq->policy->ops->attached_refcount(rq, zombie);
			if (zombie && atomic_load(&zombie->refcnt) != attached_refcnt) {
				list_remove(&zombie->state.block_link);
				list_add_tail(&rq->zombie_list, &zombie->state.block_link);
				semaphore_signal(&rq->reaper_sem);
				zombie = NULL;
			}
		}

		spinlock_release_preempt_enable(&rq->zombie_lock);
		if (zombie) {
			sched_thread_detach(zombie);
			sched_thread_destroy(zombie);
		} else {
			schedule();
		}
	}
	return 0;
}

static void reaper_init(void) {
	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* reaper_thread = kthread_create(SCHED_TOPOLOGY_CURRENT | SCHED_TOPOLOGY_NO_MIGRATE,
			reaper, rq, "reaper/%u", rq->sched_id);
	if (unlikely(!reaper_thread))
		out_of_memory();

	int err = kthread_run(reaper_thread, SCHED_PRIO_DEFAULT);
	if (unlikely(err))
		panic("Failed to create reaper thread: %d", err);
}

INIT_TASK_DECLARE(kthread_init_task, sched_init_task, sched_ap_init_task);
INIT_TASK_DEFINE(reaper_thread_init_task, INIT_TASK_SCOPE_BSP, reaper_init, &kthread_init_task, &sched_init_task);
INIT_TASK_DEFINE(reaper_thread_ap_init_task, INIT_TASK_SCOPE_AP, reaper_init, &kthread_init_task, &sched_ap_init_task);
