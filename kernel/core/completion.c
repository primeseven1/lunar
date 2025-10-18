#include <lunar/core/completion.h>
#include <lunar/sched/kthread.h>

int completion_wait(struct completion* completion, int flags) {
	if (atomic_load_explicit(&completion->done, ATOMIC_ACQUIRE))
		return 0;

	irqflags_t irq_flags;
	spinlock_lock_irq_save(&completion->lock, &irq_flags);

	/* Now use ATOMIC_RELAXED since the lock is acquired */
	if (atomic_load_explicit(&completion->done, ATOMIC_RELAXED)) {
		spinlock_unlock_irq_restore(&completion->lock, &irq_flags);
		return 0;
	}

	struct thread* thread = current_thread();
	sched_prepare_sleep(0, SCHED_SLEEP_BLOCK | flags);
	list_add(&completion->waiters, &thread->block_link);

	spinlock_unlock_irq_restore(&completion->lock, &irq_flags);

	int reason = schedule();
	if (reason == -EINTR) {
		spinlock_lock_irq_save(&completion->lock, &irq_flags);
		if (likely(list_node_linked(&thread->block_link)))
			list_remove(&thread->block_link);
		else
			reason = 0;
		spinlock_unlock_irq_restore(&completion->lock, &irq_flags);
	}
	return reason;
}

void completion_complete(struct completion* completion) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&completion->lock, &irq_flags);

	/* Use release for any threads taking the fast path on completion_wait */
	atomic_store_explicit(&completion->done, true, ATOMIC_RELEASE);

	struct thread* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &completion->waiters, block_link) {
		list_remove(&pos->block_link);
		bug(sched_wakeup(pos, 0) != 0);
	}

	spinlock_unlock_irq_restore(&completion->lock, &irq_flags);
}

void completion_reset(struct completion* completion) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&completion->lock, &irq_flags);

	bug(!list_empty(&completion->waiters));
	atomic_store_explicit(&completion->done, false, ATOMIC_RELAXED);

	spinlock_unlock_irq_restore(&completion->lock, &irq_flags);
}
