#include <crescent/core/mutex.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/init/status.h>

void mutex_lock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_lock(&lock->spinlock);
		return;
	}

	struct thread* current_thread = current_cpu()->runqueue.current;
	bug(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread);

	semaphore_wait(&lock->sem, 0);
	atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
}

int mutex_lock_timed(mutex_t* lock, time_t timeout_ms) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_lock(&lock->spinlock);
		return 0;
	}

	struct thread* current_thread = current_cpu()->runqueue.current;
	bug(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread);

	int res = semaphore_wait_timed(&lock->sem, timeout_ms, 0);
	if (res == 0)
		atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
	return res;
}

bool mutex_try_lock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED))
		return spinlock_try(&lock->spinlock);

	struct thread* current_thread = current_cpu()->runqueue.current;
	bug(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread);

	bool success = semaphore_try(&lock->sem);
	if (success)
		atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
	return success;
}

void mutex_unlock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_unlock(&lock->spinlock);
		return;
	}

	struct thread* current_thread = current_cpu()->runqueue.current;
	bug(atomic_load(&lock->owner, ATOMIC_ACQUIRE) != current_thread);

	atomic_store(&lock->owner, NULL, ATOMIC_RELEASE);
	semaphore_signal(&lock->sem);
}
