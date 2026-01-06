#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/asm/segment.h>
#include <lunar/lib/string.h>
#include <lunar/mm/vmm.h>

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
	__asm__ volatile goto("swapgs\n\t"
			"movw %0, %%gs\n\t"
			"movw %0, %%fs\n\t"
			"swapgs\n\t"
			"movw %1, %%ds\n\t"
			"movw %1, %%es\n\t"
			"movw %1, %%ss\n\t"
			"leaq %l[reload](%%rip), %%rax\n\t"
			"pushq %2\n\t"
			"pushq %%rax\n\t"
			"lretq"
			:
			: "r"((u16)0), "r"((u16)SEGMENT_KERNEL_DATA), "r"((u64)SEGMENT_KERNEL_CODE)
			: "rax", "memory"
			: reload);
reload:
	__asm__ volatile("ltr %0" : : "r"((u16)SEGMENT_TASK_STATE) : "memory");
}

void segments_init(void) {
	struct kernel_segments* segments = vmap(NULL, sizeof(*segments), MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (IS_PTR_ERR(segments))
		panic("segments_init() failed!");
	memcpy(segments->segments, base, sizeof(segments->segments));

	/* Allocate TSS + io permission bitmap */
	size_t tss_size = sizeof(struct tss_descriptor) + (65536 / 8);
	struct tss_descriptor* tss = vmap(NULL, tss_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (IS_PTR_ERR(tss))
		panic("segments_init() failed!");
	memset(tss, INT_MAX, tss_size);
	tss->iopb = sizeof(*tss);

	memset(tss->rsp, 0, sizeof(tss->rsp));
	for (int i = 0; i < 3; i++) {
		tss->ist[i] = vmap_stack(KSTACK_SIZE, true);
		if (IS_PTR_ERR(tss->ist[i]))
			panic("segments_init() failed!");
	}

	/* Now set up the TSS descriptor */
	segments->tss.desc.limit_low = sizeof(*tss);
	segments->tss.desc.base_low = (uintptr_t)tss & 0xFFFF;
	segments->tss.desc.base_middle = ((uintptr_t)tss >> 16) & 0xFF;
	segments->tss.desc.access = 0x89;
	segments->tss.desc.flags = 0;
	segments->tss.desc.base_high = ((uintptr_t)tss >> 24) & 0xFF;
	segments->tss.base_top = (uintptr_t)tss >> 32;
	segments->tss.__reserved = 0;

	/* Now just load the GDT for the CPU */
	struct {
		u16 limit;
		struct kernel_segments* pointer;
	} __attribute__((packed)) gdtr = {
		.limit = sizeof(*segments) - 1,
		.pointer = segments
	};
	__asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");

	current_cpu()->tss = tss;
	reload_segment_registers();
}
