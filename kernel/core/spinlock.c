#include <lunar/asm/wrap.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/interrupt.h>

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

void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	spinlock_lock(lock);
}

void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags) {
	spinlock_unlock(lock);
	local_irq_restore(*flags);
}

bool spinlock_try_lock_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = local_irq_save();
	if (spinlock_try_lock(lock))
		return true;
	local_irq_restore(*flags);
	return false;
}
