#pragma once

#include <crescent/common.h>
#include <crescent/mm/mm.h>

void* kmalloc(size_t size, mm_t mm_flags);
void* krealloc(void* addr, size_t new_size, mm_t mm_flags);
void kfree(void* addr);

static inline void* kzalloc(size_t size, mm_t mm_flags) {
	void* ret = kmalloc(size, mm_flags);
	if (!ret)
		return NULL;
	__builtin_memset(ret, 0, size);
	return ret;
}

void heap_init(void);
