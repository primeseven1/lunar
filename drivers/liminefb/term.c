#include <crescent/mm/heap.h>
#include <crescent/lib/string.h>
#include <crescent/core/printk.h>
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

static const char* get_lvl_string(unsigned int level) {
	switch (level) {
	case PRINTK_DBG_N:
		return "\033[32m[DBG]\033[0m ";
	case PRINTK_INFO_N:
		return "\033[97m[INFO]\033[0m ";
	case PRINTK_WARN_N:
		return "\033[33m[WARN]\033[0m ";
	case PRINTK_ERR_N:
		return "\033[31m[ERR]\033[0m ";
	case PRINTK_CRIT_N:
		return "\033[31m[CRIT]\033[0m ";
	case PRINTK_EMERG_N:
		return "\033[31m[EMERG]\033[0m ";
	}

	return NULL;
}

static void printk_hook(const struct printk_msg* msg) {
	if (msg->msg_level > msg->global_level)
		return;

	const char* lvl = get_lvl_string(msg->msg_level);
	liminefb_term_write(lvl, strlen(lvl));
	liminefb_term_write(msg->msg, strlen(msg->msg));
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

	printk_set_hook(printk_hook);
	return 0;
}
