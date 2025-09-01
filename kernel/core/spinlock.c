#include <crescent/asm/wrap.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/interrupt.h>

void spinlock_lock(spinlock_t* lock) {
	while (atomic_test_and_set(lock, ATOMIC_ACQUIRE))
		cpu_relax();
}

void spinlock_unlock(spinlock_t* lock) {
	atomic_clear(lock, ATOMIC_RELEASE);
}

bool spinlock_try(spinlock_t* lock) {
	return !atomic_test_and_set(lock, ATOMIC_ACQ_REL);
}

void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	spinlock_lock(lock);
}

void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags) {
	spinlock_unlock(lock);
	local_irq_restore(*flags);
}

bool spinlock_try_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	if (spinlock_try(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}
