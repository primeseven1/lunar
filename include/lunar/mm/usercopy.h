#pragma once

#include <lunar/types.h>
#include <lunar/compiler.h>

int usercopy_memset(void __user* dest, int val, size_t count);
int usercopy_from_user(void* dest, void __user* src, size_t count);
int usercopy_to_user(void __user* dest, void* src, size_t count);
int usercopy_strlen(const char __user* str, size_t* len);
