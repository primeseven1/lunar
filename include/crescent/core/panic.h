#pragma once

#include <crescent/compiler.h>

_Noreturn void panic(const char* fmt, ...);

#define assert(c) \
	do { \
		if (unlikely(!(c))) \
			panic("%s:%d: %s failed!", __FILE__, __LINE__, #c); \
	} while (0)
