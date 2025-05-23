#pragma once

#include <crescent/asm/errno.h>
#include <crescent/core/limine.h>
#include <crescent/core/printk.h>

void liminefb_put_pixel(struct limine_framebuffer* fb_dev, 
		u32 x, u32 y, u8 red, u8 green, u8 blue);
void liminefb_clear_screen(struct limine_framebuffer* fb_dev, u8 red, u8 green, u8 blue);
void liminefb_print_init(struct limine_framebuffer* fb);
void liminefb_printk_hook(const struct printk_msg* msg);

void limine_fb_wait_refcount_zero(void);
