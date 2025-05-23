#include <crescent/common.h>
#include <crescent/core/limine.h>
#include <crescent/core/locking.h>
#include <crescent/core/printk.h>
#include <crescent/core/io.h>
#include <crescent/lib/string.h>
#include <crescent/lib/fonts.h>
#include "video.h"

struct printk_video_dev {
	struct limine_framebuffer* framebuffer;
	const struct font_data* font;
	unsigned int max_x, max_y;
	unsigned int current_x, current_y;
};

static struct printk_video_dev initial_dev;

static void scroll_text(struct printk_video_dev* dev) {
	size_t fb_size = dev->framebuffer->width * dev->framebuffer->height;
	size_t fb_byte_pp = dev->framebuffer->bpp >> 3;
	size_t row_size = dev->framebuffer->width * fb_byte_pp * dev->font->height;

	/* Start out by copying every pixel from the second row, and moving it to the top row */
	size_t index = (dev->font->height * dev->framebuffer->pitch);
	u8 __iomem* addr = (u8*)dev->framebuffer->address + index;
	size_t count = fb_size * fb_byte_pp - row_size;
	memmove_io(dev->framebuffer->address, addr, count);

	/* Now just clear the last row */
	index = ((dev->framebuffer->height - dev->font->height) * dev->framebuffer->pitch);
	addr = (u8*)dev->framebuffer->address + index;
	count = dev->framebuffer->width * fb_byte_pp * dev->font->height;
	memset_io(addr, 0, count);
}

static void put_char(struct printk_video_dev* dev, unsigned char c, u8 red, u8 green, u8 blue) {
	u32 x = dev->current_x * dev->font->width;
	u32 y = dev->current_y * dev->font->height;

	if (unlikely(x > dev->framebuffer->width || y > dev->framebuffer->height))
		return;

	for (int row = 0; row < dev->font->height; row++) {
		for (int col = dev->font->width - 1; col >= 0; col--) {
			const u8* font_data = dev->font->font_data + c * dev->font->height;
			if (font_data[row] & (1 << col)) {
				liminefb_put_pixel(dev->framebuffer, 
						x + dev->font->width - col - 1, y + row, red, green,
						blue);
			}
		}
	}

	dev->current_x++;
	if (dev->current_x > dev->max_x) {
		dev->current_x = 0;
		dev->current_y++;
		if (dev->current_y > dev->max_y) {
			scroll_text(dev);
			dev->current_y = dev->max_y;
		}
	}
}

static bool handle_escape(struct printk_video_dev* dev, char c) {
	switch (c) {
	case '\n':
		dev->current_x = 0;
		dev->current_y++;
		if (dev->current_y > dev->max_y) {
			scroll_text(dev);
			dev->current_y = dev->max_y;
		}
		return true;
	case '\b':
		if (dev->current_x)
			dev->current_x--;
		else if (dev->current_x == 0 && dev->current_y)
			dev->current_x = dev->max_x;
		return true;
	case '\a':
		return true;
	case '\r':
		dev->current_x = 0;
		return true;
	case '\v': {
		dev->current_y++;
		if (dev->current_y > dev->max_y) {
			scroll_text(dev);
			dev->current_y = dev->max_y;
		}
		return true;
	}
	case '\t': {
		const unsigned int tab_width = 8;
		unsigned int next_tab_stop = (dev->current_x / tab_width + 1) * tab_width;

		dev->current_x = next_tab_stop;
		if (dev->current_x > dev->max_x) {
			dev->current_x = 0;
			dev->current_y++;

			if (dev->current_y > dev->max_y) {
				scroll_text(dev);
				dev->current_y = dev->max_y;
			}
		}

		return true;
	}
	}

	return false;
}

static const char* get_lvl_str_rgb(unsigned int level, u32* r, u32* g, u32* b) {
	switch (level) {
	case PRINTK_DBG_N:
		*r = 0x00;
		*g = 0x80;
		*b = 0x00;
		return "[DBG] ";
	case PRINTK_INFO_N:
		*r = 0xff;
		*g = 0xff;
		*b = 0xff;
		return "[INFO] ";
	case PRINTK_WARN_N:
		*r = 0x80;
		*g = 0x80;
		*b = 0x00;
		return "[WARN] ";
	case PRINTK_ERR_N:
		*r = 0x80;
		*g = 0x00;
		*b = 0x00;
		return "[ERR] ";
	case PRINTK_CRIT_N:
		*r = 0xcc;
		*g = 0x00;
		*b = 0x00;
		return "[CRIT] ";
	case PRINTK_EMERG_N:
		*r = 0xff;
		*g = 0x00;
		*b = 0x00;
		return "[EMERG] ";
	}

	*r = 0;
	*g = 0;
	*b = 0;
	return "";
}

void liminefb_printk_hook(const struct printk_msg* msg) {
	if (msg->msg_level > msg->global_level)
		return;

	u32 r, g, b;
	const char* lvl = get_lvl_str_rgb(msg->msg_level, &r, &g, &b);
	while (*lvl)
		put_char(&initial_dev, *lvl++, r, g, b);

	const char* m = msg->msg;
	size_t len = msg->len;

	while (len) {
		if (!handle_escape(&initial_dev, *m))
			put_char(&initial_dev, (unsigned char)*m, 0xcc, 0xcc, 0xcc);
		m++;
		len--;
	}
}

void liminefb_print_init(struct limine_framebuffer* fb) {
	initial_dev.font = &g_font_8x16;
	initial_dev.max_x = fb->width / initial_dev.font->width - 1;
	initial_dev.max_y = fb->height / initial_dev.font->height - 1;
	initial_dev.framebuffer = fb;
}
