#pragma once

#ifndef __ASSEMBLER__
#define __asmlinkage __attribute__((sysv_abi))
#endif /* __ASSEMBLER__ */

#include <arch-generic/asm/linkage.h>
