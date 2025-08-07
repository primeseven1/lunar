#include <crescent/core/mutex.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>

void mutex_lock(mutex_t* lock) {
	struct thread* current_thread = current_cpu()->current_thread;
	if (unlikely(!current_thread)) {
		spinlock_lock(&lock->spinlock);
		return;
	}

	if (unlikely(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread))
		panic("mutex_lock deadlock");
	semaphore_wait(&lock->sem);
	atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
}

int mutex_lock_timed(mutex_t* lock, time_t timeout_ms) {
	struct thread* current_thread = current_cpu()->current_thread;
	if (unlikely(!current_thread)) {
		spinlock_lock(&lock->spinlock);
		return 0;
	}

	if (unlikely(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread)) {
		printk(PRINTK_CRIT "core: thread already has lock, guaranteed timeout (%s)\n", __func__);
		return -ETIMEDOUT;
	}

	int res = semaphore_wait_timed(&lock->sem, timeout_ms);
	if (res == 0)
		atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
	return res;
}

bool mutex_try_lock(mutex_t* lock) {
	struct thread* current_thread = current_cpu()->current_thread;
	if (unlikely(!current_thread))
		return spinlock_try(&lock->spinlock);

	if (unlikely(atomic_load(&lock->owner, ATOMIC_ACQUIRE) == current_thread)) {
		printk(PRINTK_CRIT "core: thread already has lock, guaranteed fail (%s)\n", __func__);
		return false;
	}

	bool success = semaphore_try(&lock->sem);
	if (success)
		atomic_store(&lock->owner, current_thread, ATOMIC_RELEASE);
	return success;
}

void mutex_unlock(mutex_t* lock) {
	struct thread* current_thread = current_cpu()->current_thread;
	if (unlikely(!current_thread)) {
		spinlock_unlock(&lock->spinlock);
		return;
	}

	if (unlikely(atomic_load(&lock->owner, ATOMIC_ACQUIRE) != current_thread))
		panic("mutex_unlock not owner");

	atomic_store(&lock->owner, NULL, ATOMIC_RELEASE);
	semaphore_signal(&lock->sem);
}
