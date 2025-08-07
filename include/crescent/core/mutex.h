#pragma once

#include <crescent/core/semaphore.h>
#include <crescent/core/cpu.h>

typedef struct {
	struct semaphore sem;
	atomic(struct thread*) owner;
	spinlock_t spinlock;
} mutex_t;

static inline void mutex_init(mutex_t* lock) {
	semaphore_init(&lock->sem, 1);
	atomic_store(&lock->owner, NULL, ATOMIC_RELAXED);
}

/**
 * @brief Grab a mutex, and block if the mutex can't be taken right away
 * @param lock The mutex to take
 */
void mutex_lock(mutex_t* lock);

/**
 * @brief Unlock a mutex
 * @param lock The lock to release
 */
void mutex_unlock(mutex_t* lock);

/**
 * @brief Grab a mutex with a timeout
 *
 * @param lock The lock to grab
 * @param timeout_ms The number of milliseconds to wait before failing, ignored when no scheduler is initialized
 * 
 * @return -ETIMEDOUT when timing out, 0 on success
 */
int mutex_lock_timed(mutex_t* lock, time_t timeout_ms);

/**
 * @brief Try grabbing a mutex only once
 * @return true if the lock was sucessfully taken
 */
bool mutex_try_lock(mutex_t* lock);
