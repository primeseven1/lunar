#include <lunar/mutex.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/sched.h>

static atomic(bool) use_spinlock = atomic_init(true);

void mutex_acquire(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, ATOMIC_RELAXED))) {
		spinlock_acquire(&lock->spinlock);
		return;
	}

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, ATOMIC_RELAXED) == thread);

	bug(semaphore_wait(&lock->sem, 0) != 0);
	atomic_store_explicit(&lock->owner, thread, ATOMIC_RELAXED);
}

int mutex_acquire_safe(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, ATOMIC_RELAXED))) {
		spinlock_acquire(&lock->spinlock);
		return 0;
	}

	struct thread* thread = current_thread();
	if (atomic_load_explicit(&lock->owner, ATOMIC_RELAXED) == thread)
		return -EDEADLK;

	bug(semaphore_wait(&lock->sem, 0) != 0);
	return 0;
}

int mutex_acquire_timed(mutex_t* lock, time_t us) {
	if (unlikely(atomic_load_explicit(&use_spinlock, ATOMIC_RELAXED))) {
		spinlock_acquire(&lock->spinlock);
		return 0;
	}

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, ATOMIC_RELAXED) == thread);

	int res = semaphore_wait_timed(&lock->sem, us, 0);
	if (res == 0)
		atomic_store_explicit(&lock->owner, thread, ATOMIC_RELAXED);
	return res;
}

bool mutex_try_acquire(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, ATOMIC_RELAXED)))
		return spinlock_try_acquire(&lock->spinlock);

	struct thread* thread = current_thread();
	bug(atomic_load_explicit(&lock->owner, ATOMIC_RELAXED) == thread);

	bool success = semaphore_try(&lock->sem);
	if (success)
		atomic_store_explicit(&lock->owner, thread, ATOMIC_RELAXED);
	return success;
}

void mutex_release(mutex_t* lock) {
	if (unlikely(atomic_load_explicit(&use_spinlock, ATOMIC_RELAXED))) {
		spinlock_release(&lock->spinlock);
		return;
	}

	bug(atomic_exchange_explicit(&lock->owner, NULL, ATOMIC_RELAXED) != current_thread());
	semaphore_signal(&lock->sem);
}

void mutex_disable_spinlock(void) {
	atomic_store_explicit(&use_spinlock, false, ATOMIC_RELAXED);
}
