#include <crescent/asm/flags.h>
#include <crescent/core/locking.h>

void spinlock_lock(spinlock_t* lock) {
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
		__asm__ volatile("pause" : : : "memory");
}

void spinlock_unlock(spinlock_t* lock) {
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

bool spinlock_try(spinlock_t* lock) {
	if (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
		return false;
	return true;
}

void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = read_cpu_flags();
	local_irq_disable();
	spinlock_lock(lock);
}

void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags) {
	spinlock_unlock(lock);
	if (*flags & CPU_FLAG_INTERRUPT)
		local_irq_enable();
}

bool spinlock_try_irq_save(spinlock_t* lock, unsigned long* flags) {
	*flags = read_cpu_flags();
	local_irq_disable();
	if (spinlock_try(lock))
		return true;
	if (*flags & CPU_FLAG_INTERRUPT)
		local_irq_enable();
	return false;
}
