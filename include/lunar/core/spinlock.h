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
 * @brief Try acquiring a spinlock, save and disable the IRQ state, and restore IRQ's on failure
 *
 * @param lock The lock to acquire
 * @param flags A pointer to the IRQ state
 *
 * @retval true The lock was acquired, and IRQ's are disabled
 * @retval false The lock could not be acquired, and the IRQ state is what it was before trying the lock
 */
bool spinlock_try_lock_irq_save(spinlock_t* lock, irqflags_t* flags);

static inline void spinlock_init(spinlock_t* lock) {
	atomic_store_explicit(lock, false, ATOMIC_RELAXED);
}

typedef struct {
	atomic(unsigned int) readers;
	atomic(unsigned int) writers_waiting;
	atomic(bool) writer;
} rwlock_t;

#define RWLOCK_INITIALIZER { \
	.readers = atomic_init(0), \
	.writers_waiting = atomic_init(0), \
	.writer = atomic_init(false) \
}
#define RWLOCK_DEFINE(n) rwlock_t n = RWLOCK_INITIALIZER

/**
 * @brief Acquire a read on a rwlock
 * @param lock The lock to acquire a read on
 */
void rwlock_read_lock(rwlock_t* lock);

/**
 * @brief Release a read on a rwlock
 * @param lock THe lock to release the read on
 */
void rwlock_read_unlock(rwlock_t* lock);

/**
 * @brief Acquire the write lock on a rwlock
 * @param lock The lock to acquire the write on
 */
void rwlock_write_lock(rwlock_t* lock);

/**
 * @brief Release the write lock on a rwlock
 * @param lock The lock to release to write on
 */
void rwlock_write_unlock(rwlock_t* lock);

/**
 * @brief Acquire a read lock and save the IRQ state
 *
 * @param lock The lock to acquire a read on
 * @param flags The pointer to the IRQ state flags
 */
void rwlock_read_lock_irq_save(rwlock_t* lock, irqflags_t* flags);

/**
 * @brief Release a read lock and restore the IRQ state
 *
 * @param lock The lock to release the read on
 * @param flags The pointer to the IRQ state flags
 */
void rwlock_read_unlock_irq_restore(rwlock_t* lock, irqflags_t* flags);

/**
 * @brief Acquire the write lock on a rwlock, and save the IRQ state
 *
 * @param lock The lock to acquire the write on
 * @param flags The pointer to the IRQ state flags
 */
void rwlock_write_lock_irq_save(rwlock_t* lock, irqflags_t* flags);

/**
 * @brief Release the write lock on a rwlock, and restore the IRQ state
 *
 * @param lock The lock to release the write on
 * @param flags The pointer to the IRQ state flags
 */
void rwlock_write_unlock_irq_restore(rwlock_t* lock, irqflags_t* flags);

static inline void rwlock_init(rwlock_t* lock) {
	atomic_store_explicit(&lock->readers, 0, ATOMIC_RELAXED);
	atomic_store_explicit(&lock->writers_waiting, 0, ATOMIC_RELAXED);
	atomic_store_explicit(&lock->writer, false, ATOMIC_RELAXED);
}
