#include <crescent/core/cpu.h>
#include <crescent/core/semaphore.h>

int semaphore_wait(struct semaphore* sem) {
	unsigned long flags;
	spinlock_lock_irq_save(&sem->lock, &flags);

	if (--sem->count < 0) {
		struct thread* current_thread = current_cpu()->runqueue.current;
		list_add_tail(&sem->wait_queue, &current_thread->external_blocked_link);
		thread_block_noyield(false);
		spinlock_unlock_irq_restore(&sem->lock, &flags);
		return schedule();
	}

	spinlock_unlock_irq_restore(&sem->lock, &flags);
	return 0;
}

/* terrible implementation, fix later */
int semaphore_wait_timed(struct semaphore *sem, time_t timeout_ms) {
	time_t sleep_time_ms;
	do {
		if (semaphore_try(sem))
			return 0;

		sleep_time_ms = timeout_ms > 10 ? 10 : timeout_ms;
		timeout_ms -= sleep_time_ms;

		if (sleep_time_ms)
			thread_sleep(sleep_time_ms, false);
	} while (timeout_ms);

	return -ETIMEDOUT;
}

void semaphore_signal(struct semaphore* sem) {
	unsigned long irq;
	spinlock_lock_irq_save(&sem->lock, &irq);

	if (++sem->count <= 0 && !list_empty(&sem->wait_queue)) {
		struct thread* thread = list_entry(sem->wait_queue.node.next, struct thread, external_blocked_link);
		list_remove(&thread->external_blocked_link);
		thread_wakeup(thread, 0);
	}

	spinlock_unlock_irq_restore(&sem->lock, &irq);
}

bool semaphore_try(struct semaphore* sem) {
	bool ret = false;

	unsigned long irq;
	spinlock_lock_irq_save(&sem->lock, &irq);

	if (sem->count > 0) {
		sem->count--;
		ret = true;
	}

	spinlock_unlock_irq_restore(&sem->lock, &irq);
	return ret;
}
