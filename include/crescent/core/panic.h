#pragma once

#include <crescent/compiler.h>

/**
 * @brief Halt the system
 *
 * @param fmt The format string
 * @param ... Arguments for the format string
 */
__attribute__((format(printf, 1, 2)))
_Noreturn void panic(const char* fmt, ...);

#define assert(c) \
	do { \
		if (unlikely(!(c))) \
			panic("%s:%d: %s failed!", __FILE__, __LINE__, #c); \
	} while (0)

#define bug(c) \
	do { \
		if (unlikely(!!(c))) \
			panic("%s:%d: %s bug!", __FILE__, __LINE__, #c); \
	} while (0)
