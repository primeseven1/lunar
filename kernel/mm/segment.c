#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/asm/segment.h>
#include <crescent/lib/string.h>
#include <crescent/mm/vmm.h>

#define SEGMENT_COUNT 5

struct segment_descriptor {
	u16 limit_low;
	u16 base_low;
	u8 base_middle;
	u8 access;
	u8 flags;
	u8 base_high;
} __attribute__((packed));

struct tss_segment_descriptor {
	struct segment_descriptor desc;
	u32 base_top;
	u32 __reserved;
} __attribute__((packed));

struct tss_descriptor {
	u32 __reserved0;
	void* rsp[3];
	u64 __reserved1;
	void* ist[7];
	u32 __reserved2;
	u32 __reserved3;
	u16 __reserved4;
	u16 iopb;
} __attribute__((packed));

struct kernel_segments {
	struct segment_descriptor segments[SEGMENT_COUNT];
	struct tss_segment_descriptor tss;
} __attribute__((packed));

/*
 * Segment 0: NULL
 * Segment 1: Kernel code, 64 bit
 * Segment 2: Kernel data, 32 bit (irrelevant)
 * Segment 3: User code, 64 bit
 * Segment 4: User data, 32 bit (irrelevant)
 */
static const struct segment_descriptor base[SEGMENT_COUNT] = {
	{ 
		.limit_low = 0, .base_low = 0, .base_middle = 0, 
		.access = 0, .flags = 0, .base_high = 0 
	},
	{
		.limit_low = 0xFFFF, .base_low = 0, .base_middle = 0,
		.access = 0x9B, .flags = 0xAF, .base_high = 0
	},
	{
		.limit_low = 0xFFFF, .base_low = 0, .base_middle = 0,
		.access = 0x93, .flags = 0xCF, .base_high = 0
	},
	{
		.limit_low = 0xFFFF, .base_low = 0, .base_middle = 0,
		.access = 0xFB, .flags = 0xAF, .base_high = 0
	},
	{
		.limit_low = 0xFFFF, .base_low = 0, .base_middle = 0,
		.access = 0xF3, .flags = 0xCF, .base_high = 0
	}
};

/* This function was fun to write... */
static __noinline void reload_segment_registers(void) {
	__asm__ volatile("swapgs\n\t"
			"movw %0, %%gs\n\t"
			"movw %0, %%fs\n\t"
			"swapgs\n\t"
			"movw %1, %%ds\n\t"
			"movw %1, %%es\n\t"
			"movw %1, %%ss\n\t"
			"pushq %2\n\t"
			"pushq %3\n\t"
			"lretq"
			:
			: "r"((u16)0), "r"((u16)SEGMENT_KERNEL_DATA), "r"((u64)SEGMENT_KERNEL_CODE), "r"(&&reload)
			: "memory");
reload:
	__asm__ volatile("ltr %0" : : "r"((u16)SEGMENT_TASK_STATE) : "memory");
}

void segments_init(void) {
	struct kernel_segments* segments = vmap(NULL, sizeof(*segments), VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
	if (!segments)
		panic("Failed to allocate GDT on processor %lu", current_cpu()->processor_id);

	memcpy(segments->segments, base, sizeof(segments->segments));

	/* Allocate TSS + io permission bitmap */
	size_t tss_size = sizeof(struct tss_descriptor) + (65536 / 8);
	struct tss_descriptor* tss = vmap(NULL, tss_size, VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
	if (!tss)
		panic("Failed to allocate TSS!");
	memset(tss, INT_MAX, tss_size);

	size_t ist_stack_size = 0x4000;
	tss->rsp[0] = vmap(NULL, ist_stack_size, VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
	if (!tss->rsp[0])
		panic("Failed to allocate RSP0 stack!");
	tss->rsp[0] = (u8*)tss->rsp[0] + ist_stack_size;

	for (int i = 0; i < 3; i++) {
		u8* stack = vmap(NULL, ist_stack_size, VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
		if (!stack)
			panic("Failed to alloc TSS stack #%i", i + 1);
		stack += ist_stack_size;
		tss->ist[i] = stack;
	}

	tss->iopb = sizeof(*tss);

	/* Now set up the TSS descriptor */
	segments->tss.desc.limit_low = sizeof(*tss);
	segments->tss.desc.base_low = (uintptr_t)tss & 0xFFFF;
	segments->tss.desc.base_middle = ((uintptr_t)tss >> 16) & 0xFF;
	segments->tss.desc.access = 0x89;
	segments->tss.desc.flags = 0;
	segments->tss.desc.base_high = ((uintptr_t)tss >> 24) & 0xFF;
	segments->tss.base_top = (uintptr_t)tss >> 32;
	segments->tss.__reserved = 0;

	struct {
		u16 limit;
		struct kernel_segments* pointer;
	} __attribute__((packed)) gdtr = {
		.limit = sizeof(*segments) - 1,
		.pointer = segments
	};
	__asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");

	reload_segment_registers();
}
