#pragma once

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
_Static_assert((__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32),
		"(__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32)");
typedef i32 wchar_t;

#define WCHAR_MAX I32_MAX
#define WCHAR_MIN I32_MIN

/* 
 * Make sure size_t is the size of a long so ssize_t can be properly defined.
 * __LP64__ is also defined, so this should not happen.
 */
_Static_assert(sizeof(__SIZE_TYPE__) == sizeof(unsigned long),
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
