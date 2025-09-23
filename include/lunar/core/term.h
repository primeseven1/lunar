#pragma once

#include <lunar/types.h>

/**
 * @brief Register a terminal driver
 * @param write The function that writes to the term
 * @return -errno on failure
 */
int term_driver_register(void (*write)(const char*, size_t));

/**
 * @brief Uses the registered terminal driver to write
 * @param str The string to write
 * @param count The number of characters to write
 */
void term_write(const char* str, size_t count);

/**
 * @brief Enables term_write
 *
 * This function loads the driver specified in the command line,
 * and allows you to use term_write
 */
void term_init(void);
