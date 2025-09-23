#pragma once

#include <lunar/common.h>

/* After this, it's safe to assume the sizes of data types */
#ifndef __LP64__
#error "LP64 not used, cannot compile!"
#endif /* __LP64__ */

#define SCHAR_MAX 0x7f
#define SCHAR_MIN (-SCHAR_MAX - 1)
#define UCHAR_MAX 0xffU
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define USHRT_MAX 0xffffU
#define SHRT_MAX 0x7fff
#define SHRT_MIN (-SHRT_MAX - 1)
#define UINT_MAX 0xffffffffU
#define INT_MAX 0x7fffffff
#define INT_MIN (-INT_MAX - 1)
#define ULONG_MAX 0xffffffffffffffffUL
#define LONG_MAX 0x7fffffffffffffffL
#define LONG_MIN (-LONG_MAX - 1)
#define ULLONG_MAX 0xffffffffffffffffULL
#define LLONG_MAX 0x7fffffffffffffffLL
#define LLONG_MIN (-LLONG_MAX - 1)

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;
typedef long i64;
typedef unsigned long u64;
typedef __int128_t i128;
typedef __uint128_t u128;

#define U8_MAX UCHAR_MAX
#define I8_MAX SCHAR_MAX
#define I8_MIN SCHAR_MIN
#define U16_MAX USHRT_MAX
#define I16_MAX SHRT_MAX
#define I16_MIN SHRT_MIN
#define U32_MAX UINT_MAX
#define I32_MAX INT_MAX
#define I32_MIN INT_MIN
#define U64_MAX ULONG_MAX
#define I64_MAX LONG_MAX
#define I64_MIN LONG_MIN

/* Make sure wchar_t is signed and is 32 bit wide */
static_assert((__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32),
		"(__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32)");
typedef i32 wchar_t;

#define WCHAR_MAX I32_MAX
#define WCHAR_MIN I32_MIN

/* 
 * Make sure size_t is the size of a long so ssize_t can be properly defined.
 * __LP64__ is also defined, so this should not happen.
 */
static_assert(sizeof(__SIZE_TYPE__) == sizeof(unsigned long),
		"sizeof(__SIZE_TYPE__) == sizeof(unsigned long)");
typedef long ssize_t;
typedef unsigned long size_t;

#define SIZE_MAX ULONG_MAX
#define SSIZE_MAX LONG_MAX
#define SSIZE_MIN LONG_MIN

typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef u64 physaddr_t;

#define UINTPTR_MAX ULONG_MAX
#define INTPTR_MAX LONG_MAX
#define INTPTR_MIN LONG_MIN
#define PHYSADDR_MAX U64_MAX

typedef __builtin_va_list va_list;

#define va_start(v, p) __builtin_va_start(v, p)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, t) __builtin_va_arg(v, t)
#define va_copy(d, s) __builtin_va_copy(d, s)

typedef _Bool bool;

#define true 1
#define false 0

enum atomic_orders {
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
#define atomic_thread_fence(order) __atomic_thread_fence(order)

#define atomic_load(obj) __atomic_load_n(&(obj)->__x, ATOMIC_ACQUIRE)
#define atomic_store(obj, v) __atomic_store_n(&(obj)->__x, v, ATOMIC_RELEASE)
#define atomic_exchange(obj, v) __atomic_exchange_n(&(obj)->__x, v, ATOMIC_ACQ_REL)
#define atomic_test_and_set(obj) __atomic_test_and_set(&(obj)->__x, ATOMIC_ACQ_REL)
#define atomic_clear(obj) __atomic_clear(&(obj)->__x, ATOMIC_RELEASE)
#define atomic_fetch_add(obj, n) __atomic_fetch_add(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_add_fetch(obj, n) __atomic_add_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_fetch_sub(obj, n) __atomic_fetch_sub(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_sub_fetch(obj, n) __atomic_sub_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_fetch_and(obj, n) __atomic_fetch_and(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_and_fetch(obj, n) __atomic_and_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_fetch_or(obj, n) __atomic_fetch_or(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_or_fetch(obj, n) __atomic_or_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_fetch_xor(obj, n) __atomic_fetch_xor(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_xor_fetch(obj, n) __atomic_xor_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_fetch_nand(obj, n) __atomic_fetch_nand(&(obj)->__x, n, ATOMIC_ACQ_REL)
#define atomic_nand_fetch(obj, n) __atomic_nand_fetch(&(obj)->__x, n, ATOMIC_ACQ_REL)

#define atomic_load_explicit(obj, order) __atomic_load_n(&(obj)->__x, order)
#define atomic_store_explicit(obj, v, order) __atomic_store_n(&(obj)->__x, v, order)
#define atomic_exchange_explicit(obj, v, order) __atomic_exchange_n(&(obj)->__x, v, order)
#define atomic_test_and_set_explicit(obj, order) __atomic_test_and_set(&(obj)->__x, order)
#define atomic_clear_explicit(obj, order) __atomic_clear(&(obj)->__x, order)
#define atomic_fetch_add_explicit(obj, n, order) __atomic_fetch_add(&(obj)->__x, n, order)
#define atomic_add_fetch_explicit(obj, n, order) __atomic_add_fetch(&(obj)->__x, n, order)
#define atomic_fetch_sub_explicit(obj, n, order) __atomic_fetch_sub(&(obj)->__x, n, order)
#define atomic_sub_fetch_explicit(obj, n, order) __atomic_sub_fetch(&(obj)->__x, n, order)
#define atomic_fetch_and_explicit(obj, n, order) __atomic_fetch_and(&(obj)->__x, n, order)
#define atomic_and_fetch_explicit(obj, n, order) __atomic_and_fetch(&(obj)->__x, n, order)
#define atomic_fetch_or_explicit(obj, n, order) __atomic_fetch_or(&(obj)->__x, n, order)
#define atomic_or_fetch_explicit(obj, n, order) __atomic_or_fetch(&(obj)->__x, n, order)
#define atomic_fetch_xor_explicit(obj, n, order) __atomic_fetch_xor(&(obj)->__x, n, order)
#define atomic_xor_fetch_explicit(obj, n, order) __atomic_xor_fetch(&(obj)->__x, n, order)
#define atomic_fetch_nand_explicit(obj, n, order) __atomic_fetch_nand(&(obj)->__x, n, order)
#define atomic_nand_fetch_explicit(obj, n, order) __atomic_nand_fetch(&(obj)->__x, n, order)
