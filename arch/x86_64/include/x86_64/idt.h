#pragma once

#include <lunar/types.h>
#include <arch/asm/linkage.h>

#define ARCH_X86_64_IDT_ENTRY_COUNT 256
#define ARCH_X86_64_IDT_IST_COUNT 3

#define ARCH_X86_64_IDT_DIVIDE_VECTOR 0
#define ARCH_X86_64_IDT_DEBUG_VECTOR 1
#define ARCH_X86_64_IDT_NMI_VECTOR 2
#define ARCH_X86_64_IDT_BREAKPOINT_VECTOR 3
#define ARCH_X86_64_IDT_OVERFLOW_VECTOR 4
#define ARCH_X86_64_IDT_BOUND_RANGE_VECTOR 5
#define ARCH_X86_64_IDT_INVALID_OPCODE_VECTOR 6
#define ARCH_X86_64_IDT_NOFPU_VECTOR 7
#define ARCH_X86_64_IDT_DOUBLE_FAULT_VECTOR 8
#define ARCH_X86_64_IDT_BAD_TSS_VECTOR 10
#define ARCH_X86_64_IDT_NO_SEGMENT_VECTOR 11
#define ARCH_X86_64_IDT_STACK_SEGMENT_FAULT_VECTOR 12
#define ARCH_X86_64_IDT_GENERAL_PROTECTION_FAULT_VECTOR 13
#define ARCH_X86_64_IDT_PAGE_FAULT_VECTOR 14
#define ARCH_X86_64_IDT_FPUEXCEPTION_VECTOR 16
#define ARCH_X86_64_IDT_ALIGN_CHECK_VECTOR 17
#define ARCH_X86_64_IDT_MACHINE_CHECK_VECTOR 18
#define ARCH_X86_64_IDT_SIMD_EXCEPTION_VECTOR 19
#define ARCH_X86_64_IDT_VIRTUALIZATION_EXCEPTION_VECTOR 20
#define ARCH_X86_64_IDT_CONTROL_PROTECTION_VECTOR 21
#define ARCH_X86_64_IDT_HYPERVISOR_INJECT_VECTOR 28
#define ARCH_X86_64_IDT_VMM_COMMUNICATION_VECTOR 29
#define ARCH_X86_64_IDT_SECURITY_VECTOR 30
#define ARCH_X86_64_IDT_SPURIOUS_VECTOR 0xFF

struct arch_x86_64_idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 _zero;
} __attribute__((packed));
static_assert(sizeof(struct arch_x86_64_idt_entry) == 16, "sizeof(struct arch_x86_64_idt_entry) == 16");

struct arch_x86_64_idt {
	struct arch_x86_64_idt_entry entries[ARCH_X86_64_IDT_ENTRY_COUNT];
} __attribute__((packed));

void arch_x86_64_idt_init(void);
void __asmlinkage arch_x86_64_idt_reload(const struct arch_x86_64_idt* idt, size_t size);
