#pragma once

#include <lunar/common.h>
#include <lunar/types.h>

typedef u8 uacpi_u8;
typedef u16 uacpi_u16;
typedef u32 uacpi_u32;
typedef u64 uacpi_u64;
typedef i8 uacpi_i8;
typedef i16 uacpi_i16;
typedef i32 uacpi_i32;
typedef i64 uacpi_i64;

typedef bool uacpi_bool;

#define UACPI_TRUE true
#define UACPI_FALSE false

#define UACPI_NULL NULL

typedef uintptr_t uacpi_uintptr;
typedef uacpi_uintptr uacpi_virt_addr;
typedef size_t uacpi_size;

typedef va_list uacpi_va_list;
#define uacpi_va_start va_start
#define uacpi_va_end va_end
#define uacpi_va_arg va_arg

typedef char uacpi_char;

#define uacpi_offsetof __builtin_offsetof

#define UACPI_PRIu64 "llu"
#define UACPI_PRIx64 "llx"
#define UACPI_PRIX64 "llX"
#define UACPI_FMT64(val) ((unsigned long long)(val))
