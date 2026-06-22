#pragma once

#define static_assert(e, ...) _Static_assert(e, #__VA_ARGS__)
#define offsetof(t, m) __builtin_offsetof(t, m)
#define typeof(e) __typeof__(e)
#define alignof(t) _Alignof(t)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ROUND_DOWN(v, n) ((v) - ((v) % (n)))
#define ROUND_UP(v, n) ROUND_DOWN((v) + (n) - 1, n)

#define BIGGEST_ALIGNMENT __BIGGEST_ALIGNMENT__
#define NULL ((void*)0)

/**
 * @brief Round a value up to a power of two
 * @param x The value to round
 * @return The next power of two, zero is returned if overflow will occur
 */
static inline unsigned long long round_up_pow2(unsigned long long x) {
	if (x <= 1)
		return 1;
	const unsigned long long ullong_bits = sizeof(unsigned long long) * 8;
	if (x > (1ull << (ullong_bits - 1)))
		return 0;
	return 1ull << (ullong_bits - __builtin_clzll(x - 1));
}
