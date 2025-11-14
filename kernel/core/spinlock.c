#include <lunar/core/spinlock.h>
#include <lunar/asm/wrap.h>

void spinlock_lock(spinlock_t* lock) {
	while (atomic_test_and_set_explicit(lock, ATOMIC_ACQUIRE))
		cpu_relax();
}

void spinlock_unlock(spinlock_t* lock) {
	atomic_clear_explicit(lock, ATOMIC_RELEASE);
}

bool spinlock_try_lock(spinlock_t* lock) {
	return !atomic_test_and_set_explicit(lock, ATOMIC_ACQ_REL);
}

void spinlock_lock_irq_save(spinlock_t* lock, irqflags_t* flags) {
	*flags = local_irq_save();
	spinlock_lock(lock);
}

void spinlock_unlock_irq_restore(spinlock_t* lock, irqflags_t* flags) {
	spinlock_unlock(lock);
	local_irq_restore(*flags);
}

bool spinlock_try_lock_irq_save(spinlock_t* lock, irqflags_t* flags) {
	*flags = local_irq_save();
	if (spinlock_try_lock(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}

void rwlock_read_lock(rwlock_t* lock) {
	while (1) {
		while (atomic_load_explicit(&lock->writer, ATOMIC_ACQUIRE) ||
				atomic_load_explicit(&lock->writers_waiting, ATOMIC_ACQUIRE))
			cpu_relax();

		unsigned int old = atomic_load_explicit(&lock->readers, ATOMIC_RELAXED);
		if (atomic_compare_exchange_weak_explicit(&lock->readers, &old, old + 1, ATOMIC_ACQUIRE, ATOMIC_RELAXED)) {
			if (!atomic_load_explicit(&lock->writer, ATOMIC_ACQUIRE))
				return;
			atomic_sub_fetch_explicit(&lock->readers, 1, ATOMIC_RELEASE);
		}
	}
}

void rwlock_read_unlock(rwlock_t* lock) {
	atomic_sub_fetch_explicit(&lock->readers, 1, ATOMIC_RELEASE);
}

void rwlock_write_lock(rwlock_t* lock) {
	atomic_add_fetch_explicit(&lock->writers_waiting, 1, ATOMIC_ACQ_REL);
	while (1) {
		bool expected = false;
		if (atomic_compare_exchange_weak_explicit(&lock->writer, &expected, true, ATOMIC_ACQUIRE, ATOMIC_RELAXED)) {
			while (atomic_load_explicit(&lock->readers, ATOMIC_ACQUIRE))
				cpu_relax();
			break;
		}
	}

	atomic_sub_fetch_explicit(&lock->writers_waiting, 1, ATOMIC_RELEASE);
}

void rwlock_write_unlock(rwlock_t* lock) {
	atomic_store_explicit(&lock->writer, false, ATOMIC_RELEASE);
}
