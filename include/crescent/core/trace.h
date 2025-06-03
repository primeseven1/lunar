#pragma once

#include <crescent/core/interrupt.h>

int tracing_init(void);
void dump_registers(const struct context* ctx);
void dump_stack(void);
