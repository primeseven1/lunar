#pragma once

#include <arch/atomic.h>

enum atomic_order {
	ATOMIC_RELAXED = __ATOMIC_RELAXED,
	ATOMIC_CONSUME = __ATOMIC_CONSUME,
	ATOMIC_ACQUIRE = __ATOMIC_ACQUIRE,
	ATOMIC_RELEASE = __ATOMIC_RELEASE,
	ATOMIC_ACQ_REL = __ATOMIC_ACQ_REL,
	ATOMIC_SEQ_CST = __ATOMIC_SEQ_CST
};

#define atomic(type) \
	struct { \
		type __x; \
	}

#define atomic_init(v) { .__x = v }
#define atomic_thread_fence(order) arch_atomic_thread_fence(order)

#define atomic_load_explicit(obj, order) arch_atomic_load(&(obj)->__x, order)
#define atomic_store_explicit(obj, v, order) arch_atomic_store(&(obj)->__x, v, order)
#define atomic_exchange_explicit(obj, v, order) arch_atomic_exchange(&(obj)->__x, v, order)
#define atomic_flag_test_and_set_explicit(obj, order) arch_atomic_flag_test_and_set(&(obj)->__x, order)
#define atomic_flag_clear_explicit(obj, order) arch_atomic_flag_clear(&(obj)->__x, order)
#define atomic_fetch_add_explicit(obj, n, order) arch_atomic_fetch_add(&(obj)->__x, n, order)
#define atomic_add_fetch_explicit(obj, n, order) arch_atomic_add_fetch(&(obj)->__x, n, order)
#define atomic_fetch_sub_explicit(obj, n, order) arch_atomic_fetch_sub(&(obj)->__x, n, order)
#define atomic_sub_fetch_explicit(obj, n, order) arch_atomic_sub_fetch(&(obj)->__x, n, order)
#define atomic_fetch_and_explicit(obj, n, order) arch_atomic_fetch_and(&(obj)->__x, n, order)
#define atomic_and_fetch_explicit(obj, n, order) arch_atomic_and_fetch(&(obj)->__x, n, order)
#define atomic_fetch_or_explicit(obj, n, order) arch_atomic_fetch_or(&(obj)->__x, n, order)
#define atomic_or_fetch_explicit(obj, n, order) arch_atomic_or_fetch(&(obj)->__x, n, order)
#define atomic_fetch_xor_explicit(obj, n, order) arch_atomic_fetch_xor(&(obj)->__x, n, order)
#define atomic_xor_fetch_explicit(obj, n, order) arch_atomic_xor_fetch(&(obj)->__x, n, order)
#define atomic_fetch_nand_explicit(obj, n, order) arch_atomic_fetch_nand(&(obj)->__x, n, order)
#define atomic_nand_fetch_explicit(obj, n, order) arch_atomic_nand_fetch(&(obj)->__x, n, order)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
	arch_atomic_compare_exchange_strong(&(obj)->__x, expected, desired, succ, fail)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
	arch_atomic_compare_exchange_weak(&(obj)->__x, expected, desired, succ, fail)

#define atomic_load(obj) atomic_load_explicit(obj, ATOMIC_SEQ_CST)
#define atomic_store(obj, v) atomic_store_explicit(obj, v, ATOMIC_SEQ_CST)
#define atomic_exchange(obj, v) atomic_exchange_explicit(obj, v, ATOMIC_SEQ_CST)
#define atomic_flag_test_and_set(obj) atomic_flag_test_and_set_explicit(obj, ATOMIC_SEQ_CST)
#define atomic_flag_clear(obj) atomic_flag_clear_explicit(obj, ATOMIC_SEQ_CST)
#define atomic_fetch_add(obj, n) atomic_fetch_add_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_add_fetch(obj, n) atomic_add_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_fetch_sub(obj, n) atomic_fetch_sub_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_sub_fetch(obj, n) atomic_sub_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_fetch_and(obj, n) atomic_fetch_and_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_and_fetch(obj, n) atomic_and_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_fetch_or(obj, n) atomic_fetch_or_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_or_fetch(obj, n) atomic_or_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_fetch_xor(obj, n) atomic_fetch_xor_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_xor_fetch(obj, n) atomic_xor_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_fetch_nand(obj, n) atomic_fetch_nand_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_nand_fetch(obj, n) atomic_nand_fetch_explicit(obj, n, ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong(obj, expected, desired) \
	atomic_compare_exchange_strong_explicit(obj, expected, desired, ATOMIC_SEQ_CST, ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak(obj, expected, desired) \
	atomic_compare_exchange_weak_explicit(obj, expected, desired, ATOMIC_SEQ_CST, ATOMIC_SEQ_CST)
