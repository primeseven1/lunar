#include <crescent/mm/heap.h>
#include <crescent/lib/string.h>
#include <crescent/core/term.h>
#include "flanterm/src/flanterm_backends/fb.h"
#include "term.h"

static struct flanterm_context** contexts;
static u64 context_count = 0;

static void* flanterm_kmalloc(size_t size) {
	return kzalloc(size, MM_ZONE_NORMAL);	
}

static void flanterm_kfree(void* ptr, size_t size) {
	(void)size;
	kfree(ptr);
}

static void liminefb_term_write(const char* s, size_t count) {
	for (u64 i = 0; i < context_count; i++)
		flanterm_write(contexts[i], s, count);
}

int liminefb_term_init(struct limine_framebuffer_response* response) {
	if (response->framebuffer_count == 0)
		return -ENODEV;

	context_count = response->framebuffer_count;
	contexts = kzalloc(sizeof(*contexts) * context_count, MM_ZONE_NORMAL);
	if (!contexts)
		return -ENOMEM;

	for (u64 i = 0; i < context_count; i++) {
		struct limine_framebuffer* fb = response->framebuffers[i];
		contexts[i] = flanterm_fb_init(flanterm_kmalloc, flanterm_kfree, (u32*)fb->address, 
				fb->width, fb->height, fb->pitch, fb->red_mask_size, fb->red_mask_shift, 
				fb->green_mask_size, fb->green_mask_shift, fb->blue_mask_size, fb->blue_mask_shift, 
				NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 1, 0, 0, 0);
		if (!contexts[i]) {
			for (i = 0; i < context_count; i++) {
				if (!contexts[i])
					break;
				flanterm_deinit(contexts[i], flanterm_kfree);
			}
			kfree(contexts);
			return -ENOMEM;
		}
	}

	return term_driver_register(liminefb_term_write);
}
