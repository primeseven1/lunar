#pragma once

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)
#define compiler_barrier() __asm__ volatile("" : : : "memory")

#ifdef __CHECKER__
#define __user __attribute__((noderef, address_space(1)))
#define __iomem __attribute__((noderef, address_space(2)))
#define __force __attribute__((force))
#else
#define __user
#define __iomem
#define __force
#endif /* __CHECKER__ */

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(GCC diagnostic push)
#define __diag_ignore(w) __do_pragma(GCC diagnostic ignored w)
#define __diag_pop() __do_pragma(GCC diagnostic pop)
