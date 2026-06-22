#include <x86_64/fault.h>
#include <lunar/panic.h>

void arch_x86_64_double_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	(void)ctx;
	panic("Double fault\n");
}

struct extable_entry {
	i32 fault_rip_relative;
	i32 fixup_rip_relative;
} __attribute__((packed));

#define X86_MAX_INSTR_SIZE 15

extern const struct extable_entry _ld_arch_x86_64_kernel_extable_start[];
extern const struct extable_entry _ld_arch_x86_64_kernel_extable_end[];

static bool do_fixup(struct arch_context* ctx) {
	size_t count = _ld_arch_x86_64_kernel_extable_end - _ld_arch_x86_64_kernel_extable_start;

	for (size_t i = 0; i < count; i++) {
		const struct extable_entry* entry = &_ld_arch_x86_64_kernel_extable_start[i];
		uintptr_t fault = (uintptr_t)entry + offsetof(struct extable_entry, fault_rip_relative) + entry->fault_rip_relative;
		uintptr_t fixup = (uintptr_t)entry + offsetof(struct extable_entry, fixup_rip_relative) + entry->fixup_rip_relative;
		if (ctx->rip >= fault && ctx->rip < fault + X86_MAX_INSTR_SIZE) {
			ctx->rip = fixup;
			return true;
		}
	}

	return false;
}

void arch_x86_64_general_protection_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	if (do_fixup(ctx))
		return;
	panic("General protection fault\n");
}

void arch_x86_64_page_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	if (do_fixup(ctx))
		return;
	panic("Page fault\n");
}
