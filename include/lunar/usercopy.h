#pragma once

#include <arch/usercopy.h>
#include <arch/asm/errno.h>
#include <lunar/compiler.h>

#define IS_USER_ADDRESS(p) ARCH_IS_USER_ADDRESS(p)

static inline bool usercopy_access_ok(const void __user* ptr, size_t size) {
	uintptr_t end;
	if (__builtin_add_overflow((uintptr_t)ptr, size ? size - 1 : 0, &end))
		return false;
	return IS_USER_ADDRESS(ptr) && IS_USER_ADDRESS((const void __user*)end);
}

#define user_read_byte(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u8) && sizeof(*(val)) == sizeof(u8), \
				"sizeof(*(ptr)) == sizeof(u8) && sizeof(*(val)) == sizeof(u8)"); \
		usercopy_access_ok(ptr, sizeof(u8)) ? arch_user_read_byte(ptr, val) : -EFAULT; \
	})
#define user_read_word(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u16) && sizeof(*(val)) == sizeof(u16), \
				"sizeof(*(ptr)) == sizeof(u16) && sizeof(*(val)) == sizeof(u16)"); \
		usercopy_access_ok(ptr, sizeof(u16)) ? arch_user_read_word(ptr, val) : -EFAULT; \
	})
#define user_read_dword(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u32) && sizeof(*(val)) == sizeof(u32), \
				"sizeof(*(ptr)) == sizeof(u32) && sizeof(*(val)) == sizeof(u32)"); \
		usercopy_access_ok(ptr, sizeof(u32)) ? arch_user_read_dword(ptr, val) : -EFAULT; \
	})
#define user_read_qword(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u64) && sizeof(*(val)) == sizeof(u64), \
				"sizeof(*(ptr)) == sizeof(u64) && sizeof(*(val)) == sizeof(u64)"); \
		usercopy_access_ok(ptr, sizeof(u64)) ? arch_user_read_qword(ptr, val) : -EFAULT; \
	})
#define user_write_byte(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u8), "sizeof(*(ptr)) == sizeof(u8)"); \
		usercopy_access_ok(ptr, sizeof(u8)) ? arch_user_write_byte(ptr, (u8)val) : -EFAULT; \
	})
#define user_write_word(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u16), "sizeof(*(ptr)) == sizeof(u16)"); \
		usercopy_access_ok(ptr, sizeof(u16)) ? arch_user_write_word(ptr, (u16)val) : -EFAULT; \
	})
#define user_write_dword(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u32), "sizeof(*(ptr)) == sizeof(u32)"); \
		usercopy_access_ok(ptr, sizeof(u32)) ? arch_user_write_word(ptr, (u32)val) : -EFAULT; \
	})
#define user_write_qword(ptr, val) \
	({ \
		static_assert(sizeof(*(ptr)) == sizeof(u64), "sizeof(*(ptr)) == sizeof(u64)"); \
		usercopy_access_ok(ptr, sizeof(u64)) ? arch_user_write_word(ptr, (u64)val) : -EFAULT; \
	})

int usercopy_memset(void __user* dest, int val, size_t count);
int usercopy_from_user(void* dest, const void __user* src, size_t count);
int usercopy_to_user(void __user* dest, const void* src, size_t count);
ssize_t usercopy_strlen(const char __user* str);
