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
		err = asm_user_write_u8(d++, val);
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

	int rc = 0;
	u8* d = dest;
	u8 __user* s = src;
	while (count--) {
		rc = asm_user_read_u8(s++);
		if (rc < 0)
			break;
		*d++ = rc;
	}

	usercopy_exit();
	return rc < 0 ? rc : 0;
}

int usercopy_to_user(void __user* dest, void* src, size_t count) {
	if (!IS_USER_ADDRESS(dest) || IS_USER_ADDRESS(src))
		return -EFAULT;

	usercopy_enter();

	int err = 0;
	u8 __user* d = dest;
	u8* s = src;
	while (count--) {
		err = asm_user_write_u8(d++, *s++);
		if (err)
			break;
	}

	usercopy_exit();
	return -EFAULT;
}

int usercopy_strlen(const char __user* str, size_t* len) {
	if (!IS_USER_ADDRESS(str))
		return -EFAULT;

	usercopy_enter();

	int ret = 0;
	size_t _len = 0;
	const unsigned char __user* _str = (unsigned char __user*)str;
	while (1) {
		ret = asm_user_read_u8(_str + _len);
		if (ret <= 0)
			break;
		_len++;
	}

	usercopy_exit();
	if (ret == 0)
		*len = _len;
	return ret;
}
