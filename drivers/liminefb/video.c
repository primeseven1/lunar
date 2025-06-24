#include <crescent/common.h>
#include <crescent/core/limine.h>
#include <crescent/core/io.h>
#include <crescent/lib/string.h>
#include "video.h"

int liminefb_put_pixel(struct limine_framebuffer* fb_dev, 
		u32 x, u32 y, u8 red, u8 green, u8 blue) {
	if (x >= fb_dev->width || y >= fb_dev->height)
		return -EFAULT;

	u32 color = (red << fb_dev->red_mask_shift) | (green << fb_dev->green_mask_shift) |
			(blue << fb_dev->blue_mask_shift);
	u64 index = x * (fb_dev->bpp >> 3) + (y * fb_dev->pitch);
	union {
		u32 __iomem* addr32;
		u8 __iomem* addr8;
	} addr = { .addr8 = (u8 __iomem*)fb_dev->address + index };

	/* Hopefully this framebuffer has 32 bit bpp */
	if (fb_dev->bpp == 32) {
		writel(addr.addr32, color);
	} else if (fb_dev->bpp == 24) {
		writeb(addr.addr8, color & 0xFF);
		writeb(addr.addr8 + 1, (color >> 8) & 0xFF);
		writeb(addr.addr8 + 2, (color >> 16) & 0xFF);
	}

	return 0;
}

void liminefb_clear_screen(struct limine_framebuffer* fb_dev, u8 red, u8 green, u8 blue) {
	u32 color = (red << fb_dev->red_mask_shift) | (green << fb_dev->green_mask_shift) |
			(blue << fb_dev->blue_mask_shift);
	union {
		u32 __iomem* addr32;
		u8 __iomem* addr8;
	} addr = { .addr8 = (u8 __iomem*)fb_dev->address };
	size_t count;

	/* If this framebuffer doesn't have 32 bit bpp, that just kind of sucks */
	if (fb_dev->bpp == 32) {
		count = (fb_dev->width * fb_dev->height * (fb_dev->bpp >> 3)) >> 2;
		while (count--)
			writel(addr.addr32++, color);
	} else if (fb_dev->bpp == 24) {
		count = (fb_dev->width * fb_dev->height * (fb_dev->bpp >> 3)) / 3;
		while (count--) {
			writeb(addr.addr8, color & 0xFF);
			writeb(addr.addr8 + 1, (color >> 8) & 0xFF);
			writeb(addr.addr8 + 2, (color >> 16) & 0xFF);
			addr.addr8 += 3;
		}
	}
}
