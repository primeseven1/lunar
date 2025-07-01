#pragma once

#define __asmlinkage __attribute__((sysv_abi))
#define __noinline __attribute__((noinline))

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)

#define __do_pragma(x) _Pragma(#x)

#ifdef CONFIG_LLVM
#define __iomem volatile __attribute__((noderef))

#define __diag_push() __do_pragma(clang diagnostic push)
#define __diag_ignore(w) __do_pragma(clang diagnostic ignored w)
#define __diag_pop() __do_pragma(clang diagnostic pop)
#else /* CONFIG_LLVM */
#define __iomem volatile

#define __diag_push() __do_pragma(GCC diagnostic push)
#define __diag_ignore(w) __do_pragma(GCC diagnostic ignored w)
#define __diag_pop() __do_pragma(GCC diagnostic pop)
#endif /* CONFIG_LLVM */
