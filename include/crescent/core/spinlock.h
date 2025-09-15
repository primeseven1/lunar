#pragma once

#include <crescent/types.h>

typedef volatile atomic(bool) spinlock_t;

#define SPINLOCK_INITIALIZER atomic_init(false)
#define SPINLOCK_DEFINE(n) spinlock_t n = SPINLOCK_INITIALIZER

void spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);
bool spinlock_try_lock(spinlock_t* lock);
void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags);
void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags);
bool spinlock_try_lock_irq_save(spinlock_t* lock, unsigned long* flags);

static inline void spinlock_init(spinlock_t* lock) {
	atomic_store_explicit(lock, false, ATOMIC_RELAXED);
}
