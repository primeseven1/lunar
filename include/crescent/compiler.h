#pragma once

#ifdef CONFIG_LLVM
#define __iomem volatile __attribute__((noderef))
#else
#define __iomem volatile
#endif /* CONFIG_LLVM */

#define __asmlinkage __attribute__((sysv_abi))
#define __noinline __attribute__((noinline))

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)
