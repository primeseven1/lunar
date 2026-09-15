#pragma once

#define likely(c) __builtin_expect(!!(c), 1)
#define unlikely(c) __builtin_expect(!!(c), 0)
#define compiler_barrier() __asm__ volatile("" : : : "memory")

/* Clang and sparse support these attributes, GCC does not */
#if __has_attribute(noderef) && __has_attribute(address_space)

#define __user __attribute__((noderef, address_space(1)))
#define __iomem __attribute__((noderef, address_space(2)))

/* Only sparse supports __force */
#if defined(__CHECKER__)
#define __force __attribute__((force))
#else /* __CHECKER__ */
#define __force
#endif /* __CHECKER__ */

#else /* __has_attribute(noderef) && __has_attribute(address_space) */

#define __user
#define __iomem
#define __force

#endif /* __has_attribute(noderef) && __has_attribute(address_space) */

/* GCC and sparse support this attribute, clang does not */
#if __has_attribute(externally_visible)
#define __visible __attribute__((externally_visible))
#else /* __has_attribute(externally_visible) */
#define __visible
#endif /* __has_attribute(externally_visible) */

#ifdef __clang__

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(clang diagnostic push)
#define __diag_ignore(w) __do_pragma(clang diagnostic ignored w)
#define __diag_pop() __do_pragma(clang diagnostic pop)

#else /* __clang__ */

#define __do_pragma(x) _Pragma(#x)
#define __diag_push() __do_pragma(GCC diagnostic push)
#define __diag_ignore(w) __do_pragma(GCC diagnostic ignored w)
#define __diag_pop() __do_pragma(GCC diagnostic pop)

#endif /* __clang__ */
