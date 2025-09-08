#include <crescent/core/cpu.h>
#include <crescent/core/semaphore.h>

int semaphore_wait(struct semaphore* sem, int flags) {
	unsigned long irq;
	spinlock_lock_irq_save(&sem->lock, &irq);

	if (--sem->count < 0) {
		struct thread* current_thread = current_cpu()->runqueue.current;
		list_add_tail(&sem->wait_queue, &current_thread->block_link);
		sched_prepare_sleep(0, SCHED_SLEEP_BLOCK | flags);
		spinlock_unlock_irq_restore(&sem->lock, &irq);

		int reason = schedule();
		if (reason == -EINTR) {
			spinlock_lock_irq_save(&sem->lock, &irq);
			if (list_node_linked(&current_thread->block_link)) {
				list_remove(&current_thread->block_link);
				sem->count++;
			} else {
				reason = 0;
			}
			spinlock_unlock_irq_restore(&sem->lock, &irq);
		}
		return reason;
	}

	spinlock_unlock_irq_restore(&sem->lock, &irq);
	return 0;
}

int semaphore_wait_timed(struct semaphore *sem, time_t timeout_ms, int flags) {
	unsigned long irq;
	int reason = 0;
	struct thread* current_thread = current_cpu()->runqueue.current;

	spinlock_lock_irq_save(&sem->lock, &irq);

	if (--sem->count >= 0) {
		spinlock_unlock_irq_restore(&sem->lock, &irq);
		return 0;
	} else if (timeout_ms == 0) {
		sem->count++;
		spinlock_unlock_irq_restore(&sem->lock, &irq);
		return -ETIMEDOUT;
	}
	list_add_tail(&sem->wait_queue, &current_thread->block_link);
	sched_prepare_sleep(timeout_ms, SCHED_SLEEP_BLOCK | flags);

	spinlock_unlock_irq_restore(&sem->lock, &irq);

	reason = schedule();
	if (reason == -ETIMEDOUT || reason == -EINTR) {
		spinlock_lock_irq_save(&sem->lock, &irq);

		/* Check to see if the incriment was already done. If so, treat as success */
		if (list_node_linked(&current_thread->block_link)) {
			list_remove(&current_thread->block_link);
			sem->count++;
		} else {
			reason = 0;
		}

		spinlock_unlock_irq_restore(&sem->lock, &irq);
		return reason;
	}

	return reason;
}

void semaphore_signal(struct semaphore* sem) {
	unsigned long irq;
	spinlock_lock_irq_save(&sem->lock, &irq);

	if (++sem->count <= 0 && !list_empty(&sem->wait_queue)) {
		struct thread* thread = list_first_entry(&sem->wait_queue, struct thread, block_link);
		list_remove(&thread->block_link);
		assert(sched_wakeup(thread, 0) == 0);
	}

	spinlock_unlock_irq_restore(&sem->lock, &irq);
}

int semaphore_reset(struct semaphore* sem) {
	unsigned long irq;
	spinlock_lock_irq_save(&sem->lock, &irq);

	int ret = -EBUSY;
	if (!list_empty(&sem->wait_queue))
		goto out;

	ret = 0;
	sem->count = 0;
out:
	spinlock_unlock_irq_restore(&sem->lock, &irq);
	return ret;
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
