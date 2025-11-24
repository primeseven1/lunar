#include <lunar/mm/usercopy.h>
#include <lunar/sched/kthread.h>
#include "internal.h"

static inline void usercopy_enter(void) {
	current_thread()->in_usercopy = true;
	barrier();
}

static inline void usercopy_exit(void) {
	barrier();
	current_thread()->in_usercopy = false;
}

#define IS_USER_ADDRESS(p) ((uintptr_t)(p) <= 0x7FFFFFFFFFFFFFFF)

int usercopy_memset(void __user* dest, int val, size_t count) {
	if (!IS_USER_ADDRESS(dest))
		return -EFAULT;

	usercopy_enter();

	int err = 0;
	u8 __user* d = dest;
	while (count--) {
		err = write_user_8(d++, val);
		if (err)
			break;
	}

	usercopy_exit();
	return err;
}

int usercopy_from_user(void* dest, void __user* src, size_t count) {
	if (IS_USER_ADDRESS(dest) || !IS_USER_ADDRESS(src))
		return -EFAULT;

	usercopy_enter();

	u8* d = dest;
	u8 __user* s = src;
	int err = 0;
	while (count--) {
		u8 x;
		err = read_user_8(s++, &x);
		if (err)
			break;
		*d++ = x;
	}

	usercopy_exit();
	return err;
}

int usercopy_to_user(void __user* dest, void* src, size_t count) {
	if (!IS_USER_ADDRESS(dest) || IS_USER_ADDRESS(src))
		return -EFAULT;

	usercopy_enter();

	int err = 0;
	u8 __user* d = dest;
	u8* s = src;
	while (count--) {
		err = write_user_8(d++, *s++);
		if (err)
			break;
	}

	usercopy_exit();
	return err;
}

ssize_t usercopy_strlen(const char __user* str) {
	if (!IS_USER_ADDRESS(str))
		return -EFAULT;

	usercopy_enter();

	int err = 0;
	size_t len = 0;
	while (1) {
		char ch;
		err = read_user_8(&str[len], &ch);
		if (err || ch == '\0')
			break;
		len++;
	}

	usercopy_exit();
	return err ? err : (ssize_t)len;
}
