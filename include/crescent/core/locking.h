#pragma once

#include <crescent/types.h>

typedef volatile unsigned long spinlock_t;

#define SPINLOCK_INITIALIZER 0

void spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);
bool spinlock_try(spinlock_t* lock);
void spinlock_lock_irq_save(spinlock_t* lock, unsigned long* flags);
void spinlock_unlock_irq_restore(spinlock_t* lock, unsigned long* flags);
bool spinlock_try_irq_save(spinlock_t* lock, unsigned long* flags);
