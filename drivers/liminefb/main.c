#include <crescent/common.h>
#include <crescent/core/module.h>
#include <crescent/core/limine.h>
#include "video.h"

static volatile struct limine_framebuffer_request __limine_request fb_request = {
	.request.id = LIMINE_FRAMEBUFFER_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static int liminefb_init(void) {
	struct limine_framebuffer_response* fb_response = fb_request.response;
	if (unlikely(!fb_response))
		return -ENOPROTOOPT;
	if (unlikely(!fb_response->framebuffer_count))
		return -ENODEV;

	/* Check for a framebuffer that this driver knows how to properly use */
	struct limine_framebuffer* fb_dev = NULL;
	for (u64 i = 0; i < fb_response->framebuffer_count; i++) {
		struct limine_framebuffer* dev = fb_response->framebuffers[i];
		if (dev->memory_model != LIMINE_FRAMEBUFFER_RGB ||
				(dev->bpp != 24 && dev->bpp != 32) || dev->red_mask_size != 8 ||
				dev->green_mask_size != 8 || dev->blue_mask_size != 8)
			continue;

		fb_dev = dev;
		break;
	}

	/* No supported framebuffer found */
	if (unlikely(!fb_dev))
		return -ENOSYS;

	liminefb_print_init(fb_dev);
	liminefb_clear_screen(fb_dev, 0, 0, 0);

	return printk_set_hook(liminefb_printk_hook);
}

MODULE("liminefb", true, liminefb_init);
