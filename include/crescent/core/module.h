#pragma once

#include <crescent/types.h>
#include <crescent/init/status.h>

struct module {
	const char* name;
	int init_status;
	int (*init)(void);
};

/**
 * @brief Attempt to load a peice of code optionally compiled into the kernel
 * @param name The name of the module
 * @return -errno on failure
 */
int module_load(const char* name);

#define MODULE(n, is, i) \
	__attribute__((section(".modules"), aligned(8), used)) \
	static volatile struct module ___module_struct = { \
		.name = n, \
		.init_status = is, \
		.init = i \
	}
