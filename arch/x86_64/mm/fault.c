#include <lunar/sched.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/format.h>
#include <x86_64/fault.h>

#define PAGE_FAULT_WAS_PRESENT (1 << 0)
#define PAGE_FAULT_CAUSED_BY_WRITE (1 << 1)
#define PAGE_FAULT_IN_USERSPACE (1 << 2)
#define PAGE_FAULT_RESERVED_PTE_BIT_SET (1 << 3)
#define PAGE_FAULT_CAUSED_BY_INSTRUCTION_FETCH (1 << 4)
#define PAGE_FAULT_CAUSED_BY_PK_VIOLATION (1 << 5)
#define PAGE_FAULT_CAUSED_BY_SHADOW_STACK (1 << 6)
#define PAGE_FAULT_CAUSED_BY_SGX (1 << 15)

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

static int format_reason(char* buf, size_t bufsize, u64 err) {
	static const struct {
		u64 flag;
		const char* set, *clear;
	} flags[] = {
		{ PAGE_FAULT_WAS_PRESENT, "present", NULL }, { PAGE_FAULT_CAUSED_BY_WRITE, "write", "read" },
		{ PAGE_FAULT_IN_USERSPACE, "user", "kernel" }, { PAGE_FAULT_RESERVED_PTE_BIT_SET, "reserved_pte", NULL },
		{ PAGE_FAULT_CAUSED_BY_INSTRUCTION_FETCH, "instruction_fetch", NULL }, { PAGE_FAULT_CAUSED_BY_PK_VIOLATION, "pk_violation", NULL },
		{ PAGE_FAULT_CAUSED_BY_SHADOW_STACK, "shadow_stack", NULL }, { PAGE_FAULT_CAUSED_BY_SGX, "sgx", NULL }
	};

	if (bufsize == 0)
		return -EOVERFLOW;

	buf[0] = '\0';
	for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); i++) {
		const char* string = (err & flags[i].flag) ? flags[i].set : flags[i].clear;
		if (!string)
			continue;

		if (strlen(buf) > 0) {
			size_t count = strlcat(buf, ",", bufsize);
			if (count != strlen(buf))
				return -EOVERFLOW;
		}
		size_t count = strlcat(buf, string, bufsize);
		if (count != strlen(buf))
			return -EOVERFLOW;
	}

	return 0;
}

void arch_x86_64_page_fault(struct isr* isr, struct arch_context* ctx) {
	(void)isr;
	if (do_fixup(ctx))
		return;

	char buf[64];
	int err = format_reason(buf, sizeof(buf), ctx->err_code);
	if (err)
		printk(PRINTK_WARN "mm: Failed to format page fault reason: %d\n", err);

	printk(PRINTK_CRIT "Page fault at RIP %#.20lx, CR2 %#.20lx (%s)\n", ctx->rip, ctx->cr2, buf);
	panic("Page fault\n");
}
