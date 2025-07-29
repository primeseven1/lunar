#pragma once

#define __asmlinkage __attribute__((sysv_abi))
#define __noinline __attribute__((noinline))

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)

/* Used by sparse */
#define __user __attribute__((noderef, address_space(1)))
#define __iomem __attribute__((noderef, address_space(2)))
#define __force __attribute__((force))

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(GCC diagnostic push)
#define __diag_ignore(w) __do_pragma(GCC diagnostic ignored w)
#define __diag_pop() __do_pragma(GCC diagnostic pop)
