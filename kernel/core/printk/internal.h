#pragma once

#include <lunar/core/printk.h>

void printk_call_hooks(const struct printk_msg* msg);
void printk_add_to_ringbuffer(struct printk_msg* msg);
