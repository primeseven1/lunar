#include <crescent/core/timekeeper.h>
#include <crescent/core/cpu.h>
#include <crescent/asm/cpuid.h>
#include <crescent/mm/heap.h>

static bool tsc_usable(void) {
	u32 eax, edx, _unused;
	cpuid(CPUID_EXT_LEAF_HIGHEST_FUNCTION, 0, &eax, &_unused, &_unused, &_unused);
	if (eax < CPUID_EXT_LEAF_PM_FEATURES)
		return false;

	cpuid(CPUID_EXT_LEAF_PM_FEATURES, 0, &_unused, &_unused, &_unused, &edx);
	return !!(edx & (1 << 8));
}

static u64 rdtsc_serialized(void) {
	u32 low, high;
	__asm__("cpuid\n\t" 
			"rdtsc\n\t"
			: "=a"(low), "=d"(high)
			:
			: "memory", "ebx", "ecx");
	return ((u64)high << 32) | low;
}

static time_t get_ticks(void) {
	struct timekeeper_source* source = current_cpu()->timekeeper;
	time_t offset;
	if (sizeof(time_t) <= sizeof(source->private))
		offset = (time_t)source->private;
	else
		offset = *(time_t*)source->private;
	return rdtsc_serialized() - offset;
}

static inline unsigned long long get_freq_from_cpuid(u32 eax, u32 ebx, u32 ecx) {
	if (eax == 0 || ebx == 0 || ecx == 0)
		return 0;
	return ecx * ebx / eax;
}

static inline unsigned long long get_freq_from_calibration(void) {
	time_t usec = 10000;
	u64 start = rdtsc_serialized();
	timekeeper_stall(usec);
	u64 end = rdtsc_serialized();
	return (time_t)((end - start) * 1000000ull / usec);
}

static time_t get_bsp_offset(void) {
	if (current_cpu()->sched_processor_id == 0)
		return rdtsc_serialized();

	const struct smp_cpus* cpus = smp_cpus_get();
	struct cpu* bsp = cpus->cpus[0];
	if (sizeof(time_t) <= sizeof(bsp->timekeeper->private))
		return (time_t)bsp->timekeeper->private;

	return *(time_t*)bsp->timekeeper->private;
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

	time_t off = get_bsp_offset();
	if (sizeof(time_t) <= sizeof(source->private)) {
		source->private = (void*)(time_t)off;
	} else {
		source->private = kmalloc(sizeof(time_t), MM_ZONE_NORMAL);
		if (!source->private) {
			kfree(source);
			return -ENOMEM;
		}
		*(time_t*)source->private = (time_t)off;
	}

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
