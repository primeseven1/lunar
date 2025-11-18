#pragma once

#include <lunar/core/interrupt.h>

void page_fault_isr(struct isr* isr, struct context* ctx);
void gp_fault_isr(struct isr* isr, struct context* ctx);
