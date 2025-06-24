#pragma once

#include <crescent/core/interrupt.h>

void do_trap(const struct isr* isr, struct context* ctx);
