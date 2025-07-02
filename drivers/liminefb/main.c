#include <crescent/common.h>
#include <crescent/core/module.h>
#include <crescent/core/limine.h>
#include "term.h"

static volatile struct limine_framebuffer_request __limine_request fb_request = {
	.request.id = LIMINE_FRAMEBUFFER_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static int liminefb_init(void) {
	struct limine_framebuffer_response* fb_response = fb_request.response;
	if (unlikely(!fb_response))
		return -ENOPROTOOPT;

	return liminefb_term_init(fb_response);
}

MODULE("liminefb", INIT_STATUS_MM, liminefb_init);
