#pragma once

#include <lunar/core/printk.h>

void printk_call_hooks(const struct printk_msg* msg);

void printk_add_to_ringbuffer(struct printk_msg* msg);
void printk_handle_message_early(struct printk_msg* msg);
void printk_rb_init(void);

void __printk_in_panic(void);
