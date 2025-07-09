#pragma once

/**
 * @brief Parse the kernel command line
 */
int cmdline_parse(void);

/**
 * @brief Get an argument from the kernel command line
 * @param arg The argument to retrive
 * @return The value of the argument, NULL on no argument
 */
const char* cmdline_get(const char* arg);
