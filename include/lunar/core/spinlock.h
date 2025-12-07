#pragma once

#include <lunar/core/irq.h>

typedef atomic(bool) spinlock_t;

#define SPINLOCK_INITIALIZER atomic_init(false)
#define SPINLOCK_DEFINE(n) spinlock_t n = SPINLOCK_INITIALIZER

/**
 * @brief Acquire a spinlock
 * @param lock The lock to acquire
 */
void spinlock_lock(spinlock_t* lock);

/**
 * @brief Release a spinlock
 * @param lock The lock to release
 */
void spinlock_unlock(spinlock_t* lock);

/**
 * @brief Try to acquire a spinlock, but don't spin if the lock is taken
 * @param lock The lock to acquire
 * @retval true The lock was acquired
 * @retval false The lock failed to be acquired
 */
bool spinlock_try_lock(spinlock_t* lock);

/**
 * @brief Acquire a spinlock, disable and save the previous IRQ state
 *
 * @param lock The lock to acquire
 * @param flags A pointer to a stack allocated variable for the IRQ state
 */
void spinlock_lock_irq_save(spinlock_t* lock, irqflags_t* flags);

/**
 * @brief Release a spinlock, and restore the IRQ state
 *
 * @param lock The lock to release
 * @param flags The pointer to the IRQ state
 */
void spinlock_unlock_irq_restore(spinlock_t* lock, irqflags_t* flags);

/**
 * @brief Try acquiring a spinlock, save and disable the IRQ state
 *
 * On failure, the IRQ state is restored
 *
 * @param lock The lock to acquire
 * @param flags A pointer to the IRQ state
 *
 * @retval true The lock was acquired
 * @retval false The lock could not be acquired
 */
bool spinlock_try_lock_irq_save(spinlock_t* lock, irqflags_t* flags);

/**
 * @brief Acquire a spinlock, but disable preempt
 *
 * This is NOT safe to use when init_status_get() < INIT_STATUS_SCHED
 *
 * @param lock The lock to acquire
 */
void spinlock_lock_preempt_disable(spinlock_t* lock);

/**
 * @brief Release a spinlock, but enable preempt afterwards
 *
 * Only safe when used with spinlock_lock_preempt_disable
 *
 * @param lock The lock to acquire
 */
void spinlock_unlock_preempt_enable(spinlock_t* lock);

/**
 * @brief Try acquiring a spinlock, and disable preempt
 *
 * On failure to acquire the lock, preempt_enable() is called
 *
 * @param lock The lock to acquire
 *
 * @retval true The lock was acquired
 * @retval false The lock failed to be acquired
 */
bool spinlock_try_lock_preempt_disable(spinlock_t* lock);

static inline void spinlock_init(spinlock_t* lock) {
	atomic_store_explicit(lock, false, ATOMIC_RELAXED);
}
