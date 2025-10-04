#include <lunar/core/timekeeper.h>
#include <lunar/core/cpu.h>
#include <lunar/core/cmdline.h>
#include <lunar/asm/cpuid.h>
#include <lunar/mm/heap.h>

static bool tsc_usable(void) {
	/* On CPU's with multiple cores, the TSC's may have issues with drift, so this timekeeper can be disabled */
	const char* cmdline_tsc = cmdline_get("timekeeper.tsc_enable");
	if (cmdline_tsc && *cmdline_tsc == '0')
		return false;

	u32 eax, edx, _unused;
	cpuid(CPUID_EXT_LEAF_HIGHEST_FUNCTION, 0, &eax, &_unused, &_unused, &_unused);
	if (eax < CPUID_EXT_LEAF_PM_FEATURES)
		return false;

	cpuid(CPUID_EXT_LEAF_PM_FEATURES, 0, &_unused, &_unused, &_unused, &edx);
	return !!(edx & (1 << 8)); /* Check for invariant TSC */
}

static u64 rdtsc_serialized(void) {
	u32 low, high;
	__asm__("cpuid\n\t" /* cpuid is a serializing instruction */
			"rdtsc\n\t"
			: "=a"(low), "=d"(high)
			:
			: "memory", "ebx", "ecx");
	return ((u64)high << 32) | low;
}

struct tsc_priv {
	u64 offset;
};

static time_t get_ticks(void) {
	struct timekeeper_source* source = current_cpu()->timekeeper;
	u64 offset = ((struct tsc_priv*)source->private)->offset;
	return rdtsc_serialized() - offset;
}

static inline unsigned long long get_freq_from_cpuid(u32 eax, u32 ebx, u32 ecx) {
	if (eax == 0 || ebx == 0 || ecx == 0)
		return 0;

	return (unsigned long long)ecx * ebx / eax;
}

static inline unsigned long long get_freq_from_calibration(void) {
	time_t usec = 100000;
	u64 start = rdtsc_serialized();
	timekeeper_stall(usec); /* Safe since this isn't suitable as an early timekeeper */
	u64 end = rdtsc_serialized();
	return (unsigned long long)((end - start) * 1000000ull / usec);
}

static u64 get_bsp_offset(void) {
	if (current_cpu()->sched_processor_id == 0)
		return rdtsc_serialized();

	const struct smp_cpus* cpus = smp_cpus_get();
	struct cpu* bsp = cpus->cpus[0];
	return ((struct tsc_priv*)bsp->timekeeper->private)->offset;
}

static int init(struct timekeeper_source** out) {
	if (!tsc_usable())
		return -ENODEV;

	struct timekeeper_source* source = kmalloc(sizeof(*source), MM_ZONE_NORMAL);
	if (!source)
		return -ENOMEM;

	unsigned long long freq;

	u32 eax, ebx, ecx, _unused;
	cpuid(CPUID_LEAF_HIGHEST_FUNCTION, 0, &eax, &_unused, &_unused, &_unused);
	if (eax >= CPUID_LEAF_TSC_FREQ) {
		cpuid(0x15, 0, &eax, &ebx, &ecx, &_unused);
		freq = get_freq_from_cpuid(eax, ebx, ecx);
		if (freq == 0)
			freq = get_freq_from_calibration();
	} else {
		freq = get_freq_from_calibration();
	}

	source->private = kmalloc(sizeof(struct tsc_priv), MM_ZONE_NORMAL);
	if (!source->private) {
		kfree(source);
		return -ENOMEM;
	}
	((struct tsc_priv*)source->private)->offset = get_bsp_offset();

	source->get_ticks = get_ticks;
	source->freq = freq;
	*out = source;
	return 0;
}

static struct timekeeper __timekeeper tsc_timekeeper = {
	.name = "tsc",
	.init = init,
	.rating = 90, /* Good with invariant TSC, and very fast */
	.early = false
};
