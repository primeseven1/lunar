#include <lunar/lib/convert.h>

int kulltostr(char* dest, unsigned long long x, unsigned int base, size_t dsize) {
	if (dsize == 0)
		return -EOVERFLOW;
	*dest = '\0';
	if (dsize == 1)
		return -EOVERFLOW;
	const char* digits = "0123456789abcdef";
	if (base < 2 || base > __builtin_strlen(digits))
		return -EINVAL;

	/* Do the actual conversion */
	char* d = dest;
	do {
		if (dsize == 1)
			break;
		*d++ = digits[x % base];
		x /= base;
		dsize--;
	} while (x);
	*d = '\0';

	/* Make sure the conversion wasn't cut off by dsize */
	if (x)
		return -EOVERFLOW;

	/* Now reverse the string */
	d--;
	while (dest < d) {
		char tmp = *dest;
		*dest++ = *d;
		*d-- = tmp;
	}

	return 0;
}

int klltostr(char* dest, long long x, unsigned int base, size_t dsize) {
	if (x < 0) {
		if (dsize == 0)
			return -EOVERFLOW;
		if (--dsize == 0) {
			*dest = '\0';
			return -EOVERFLOW;
		}
		*dest++ = '-';
		x = -x;
	}

	return kulltostr(dest, (unsigned long long)x, base, dsize);
}
