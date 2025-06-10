#pragma once

#include <crescent/core/interrupt.h>

#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

/**
 * @brief Handle a spurious IRQ from the Intel 8259 PIC
 */
void i8259_spurious_eoi(const struct isr* isr);

/**
 * @brief Initialize the Intel 8259 PIC
 *
 * This function remaps 16 IRQ's to interrupt vector 0x20-0x30.
 * This function also will mask all IRQ's.
 */
void i8259_init(void);
