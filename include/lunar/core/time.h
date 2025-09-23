#pragma once

typedef long long time_t;
typedef long suseconds_t;

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

struct timeval {
	time_t tv_sec;
	suseconds_t tv_usec;
};

static inline time_t timespec_to_ns(const struct timespec* ts) {
	return ts->tv_sec * 1000000000ll + ts->tv_nsec;
}
