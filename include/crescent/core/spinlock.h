#pragma once

#include <crescent/types.h>

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
void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags);

/**
 * @brief Release a spinlock, and restore the IRQ state
 *
 * @param lock The lock to release
 * @param flags The pointer to the IRQ state
 */
void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags);

/**
 * @brief Try acquiring a spinlock, save and disable the IRQ state, and restore IRQ's on failure
 *
 * @param lock The lock to acquire
 * @param flags A pointer to the IRQ state
 *
 * @retval true The lock was acquired, and IRQ's are disabled
 * @retval false The lock could not be acquired, and the IRQ state is what it was before trying the lock
 */
bool spinlock_try_lock_irq_save(spinlock_t* lock, unsigned long* flags);

static inline void spinlock_init(spinlock_t* lock) {
	atomic_store_explicit(lock, false, ATOMIC_RELAXED);
}
