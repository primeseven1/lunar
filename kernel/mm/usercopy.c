#include <lunar/usercopy.h>

int usercopy_memset(void __user* dest, int val, size_t count) {
	int err = 0;
	u8 __user* d = dest;
	while (count--) {
		err = user_write_byte(d++, val);
		if (err)
			break;
	}

	return err;
}

int usercopy_from_user(void* dest, const void __user* src, size_t count) {
	if (IS_USER_ADDRESS(dest))
		return -EFAULT;

	u8* d = dest;
	const u8 __user* s = src;
	while (count--) {
		u8 x;
		int err = user_read_byte(s++, &x);
		if (err)
			return err;
		*d++ = x;
	}

	return 0;
}

int usercopy_to_user(void __user* dest, const void* src, size_t count) {
	if (IS_USER_ADDRESS(src))
		return -EFAULT;

	u8 __user* d = dest;
	const u8* s = src;
	while (count--) {
		int err = user_write_byte(d++, *s++);
		if (err)
			return err;
	}

	return 0;
}

ssize_t usercopy_strlen(const char __user* str) {
	int err = 0;
	size_t len = 0;
	while (1) {
		char ch;
		err = user_read_byte(&str[len], &ch);
		if (err || ch == '\0')
			break;
		len++;
	}

	return err ? err : (ssize_t)len;
}
