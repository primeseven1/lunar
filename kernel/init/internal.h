#pragma once

#include <lunar/core/limine.h>

_Noreturn __asmlinkage void ap_kernel_main(struct limine_mp_info* mp_info);
_Noreturn __asmlinkage void kernel_main(void);
