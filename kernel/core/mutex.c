#include <crescent/core/mutex.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/init/status.h>
#include <crescent/sched/kthread.h>

static inline void mutex_set_owner(mutex_t* lock, struct thread* thread) {
	atomic_store(&lock->owner, thread, ATOMIC_RELEASE);
}

static inline struct thread* mutex_get_owner(mutex_t* lock) {
	return atomic_load(&lock->owner, ATOMIC_ACQUIRE);
}

void mutex_lock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_lock(&lock->spinlock);
		return;
	}

	struct thread* thread = current_thread();
	bug(mutex_get_owner(lock) == thread);
	bug(semaphore_wait(&lock->sem, 0) != 0);
	mutex_set_owner(lock, thread);
}

int mutex_lock_timed(mutex_t* lock, time_t timeout_ms) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_lock(&lock->spinlock);
		return 0;
	}

	struct thread* thread = current_thread();
	bug(mutex_get_owner(lock) == thread);

	int res = semaphore_wait_timed(&lock->sem, timeout_ms, 0);
	if (res == 0)
		mutex_set_owner(lock, thread);
	return res;
}

bool mutex_try_lock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED))
		return spinlock_try(&lock->spinlock);

	struct thread* thread = current_thread();
	bug(mutex_get_owner(lock) == thread);

	bool success = semaphore_try(&lock->sem);
	if (success)
		mutex_set_owner(lock, thread);
	return success;
}

void mutex_unlock(mutex_t* lock) {
	if (unlikely(init_status_get() < INIT_STATUS_SCHED)) {
		spinlock_unlock(&lock->spinlock);
		return;
	}

	struct thread* thread = current_thread();
	bug(mutex_get_owner(lock) != thread);
	mutex_set_owner(lock, NULL);

	semaphore_signal(&lock->sem);
}
