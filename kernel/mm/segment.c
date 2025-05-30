#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/asm/segment.h>
#include <crescent/lib/string.h>
#include <crescent/mm/vmm.h>

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
	struct segment_descriptor segments[6];
	struct tss_segment_descriptor tss;
} __attribute__((packed));

/*
 * Segment 0: NULL
 * Segment 1: Kernel code, 64 bit
 * Segment 2: Kernel data, 32 bit (irrelevant)
 * Segment 3: User code, 64 bit
 * Segment 4: User code, 32 bit
 * Segment 5: User data, 32 bit (irrelevant)
 */
static const struct segment_descriptor base[6] = {
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
		.access = 0xFA, .flags = 0xAF, .base_high = 0
	},
	{
		.limit_low = 0xFFFF, .base_low = 0, .base_middle = 0,
		.access = 0xFA, .flags = 0xCF, .base_high = 0
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
	struct kernel_segments* segments = kmap(MM_ZONE_NORMAL, sizeof(*segments), MMU_READ | MMU_WRITE);
	if (!segments)
		panic("Failed to allocate GDT on processor %lu", current_cpu()->processor_id);

	memcpy(segments->segments, base, sizeof(segments->segments));

	/* Allocate TSS + io permission bitmap */
	size_t tss_size = sizeof(struct tss_descriptor) + (65536 / 8);
	struct tss_descriptor* tss = kmap(MM_ZONE_NORMAL, tss_size, MMU_READ | MMU_WRITE);
	if (!tss)
		panic("Failed to allocate TSS!");

	memset(tss, INT_MAX, tss_size);
	size_t ist_stack_size = 0x4000;
	u8* rsp0 = kmap(MM_ZONE_NORMAL, ist_stack_size, MMU_READ | MMU_WRITE);
	u8* ist1 = kmap(MM_ZONE_NORMAL, ist_stack_size, MMU_READ | MMU_WRITE);
	u8* ist2 = kmap(MM_ZONE_NORMAL, ist_stack_size, MMU_READ | MMU_WRITE);
	u8* ist3 = kmap(MM_ZONE_NORMAL, ist_stack_size, MMU_READ | MMU_WRITE);
	if (!rsp0 || !ist1 || !ist2 || !ist3)
		panic("Failed to allocate TSS stacks");

	/* Now get the virtual addresses of the stacks, and add the size since the stack grows down */
	tss->rsp[0] = rsp0 + ist_stack_size;
	tss->ist[0] = ist1 + ist_stack_size;
	tss->ist[1] = ist2 + ist_stack_size;
	tss->ist[2] = ist3 + ist_stack_size;
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
