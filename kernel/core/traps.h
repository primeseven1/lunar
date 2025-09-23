#pragma once

#include <lunar/core/interrupt.h>

void do_trap(struct isr* isr, struct context* ctx);
