#pragma once

#define ARCH_X86_64_SEGMENT_KERNEL_CODE 0x08
#define ARCH_X86_64_SEGMENT_KERNEL_DATA 0x10
#define ARCH_X86_64_SEGMENT_USER_CODE 0x18
#define ARCH_X86_64_SEGMENT_USER_DATA 0x20
#define ARCH_X86_64_SEGMENT_TASK_STATE 0x28

#define ARCH_X86_64_SEGMENT_CPL0 0
#define ARCH_X86_64_SEGMENT_CPL3 3

#ifndef __ASSEMBLER__

#include <lunar/types.h>
#include <arch/asm/linkage.h>

struct arch_x86_64_segment_descriptor {
	u16 limit_low;
	u16 base_low;
	u8 base_middle;
	u8 access;
	u8 flags;
	u8 base_high;
} __attribute__((packed));
static_assert(sizeof(struct arch_x86_64_segment_descriptor) == 8, "sizeof(struct arch_x86_64_segment_descriptor) == 8");

struct arch_x86_64_tss_descriptor {
	struct arch_x86_64_segment_descriptor desc;
	u32 base_high, _unused;
} __attribute__((packed));
static_assert(sizeof(struct arch_x86_64_tss_descriptor) == 16, "sizeof(struct arch_x86_64_tss_descriptor) == 16");

struct arch_x86_64_gdt {
	struct arch_x86_64_segment_descriptor base[5];
	struct arch_x86_64_tss_descriptor tss;
} __attribute__((packed));

struct arch_x86_64_tss {
	u32 _unused0;
	void* rsp[3];
	u64 _unused1;
	void* ist[7];
	u64 _unused2;
	u16 _unused3;
	u16 iomap_base;
} __attribute__((packed));
static_assert(sizeof(struct arch_x86_64_tss) == 104, "sizeof(struct arch_x86_64_tss) == 104");

void arch_x86_64_gdt_init(void);
void __asmlinkage arch_x86_64_gdt_reload(const struct arch_x86_64_gdt* gdt, size_t size);

#endif /* __ASSEMBLER__ */
