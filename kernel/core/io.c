#include <crescent/core/io.h>

void __iomem* memset_io(void __iomem* dest, int val, size_t count) {
	u8 __iomem* d = dest;
	while (count--)
		writeb(d++, val);
	return dest;
}

void __iomem* memmove_io(void __iomem* dest, const void __iomem* src, size_t count) {
	u8 __iomem* d = dest;
	const u8 __iomem* s = src;

	if (d < s) {
		while (count--)
			writeb(d++, readb(s++));
	} else {
		d += count;
		s += count;

		while (count--)
			writeb(--d, readb(--s));
	}

	return dest;
}
