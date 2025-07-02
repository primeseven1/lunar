#pragma once

#include <crescent/types.h>

int term_driver_register(void (*write)(const char*, size_t));
void term_write(const char* str, size_t count);
void term_init(void);
