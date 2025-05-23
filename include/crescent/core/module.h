#pragma once

#include <crescent/types.h>

struct module {
	const char* name;
	bool early;
	int (*init)(void);
};

int module_load(const char* name);

#define MODULE(n, e, i) \
	__attribute__((section(".modules"), aligned(8), used)) \
	static struct module ___module_struct = { \
		.name = n, \
		.early = e, \
		.init = i \
	}
