#include <lunar/asm/segment.h>
#include <lunar/core/printk.h>
#include <lunar/core/trace.h>
#include <lunar/core/traps.h>
#include <lunar/sched/kthread.h>

enum mmu_err_flags {
	MMU_ERR_PRESENT = (1 << 0),
	MMU_ERR_WRITE_ACCESS = (1 << 1),
	MMU_ERR_USER = (1 << 2),
	MMU_ERR_RSVD_SET_PTE = (1 << 3),
	MMU_ERR_EXEC = (1 << 4),
	MMU_ERR_PK = (1 << 5),
	MMU_ERR_SGX = (1 << 6)
};

static void exec_page_fault(const struct context* ctx) {
	uintptr_t fault = ctx->cr2;
	int err = ctx->err_code;

	struct vma* vma = vma_find(current_thread()->proc->mm_struct, fault);
	if (!vma) {
		printk(PRINTK_CRIT "traps: No VMA found at %#.16lx\n", fault);
	} else {
		if (vma->flags & MMU_READ)
			printk(PRINTK_CRIT "traps: %#.16lx is readable\n", fault);
		if (vma->flags & MMU_WRITE)
			printk(PRINTK_CRIT "traps: %#.16lx is writable\n", fault);
		if (vma->flags & MMU_EXEC)
			printk(PRINTK_CRIT "traps: %#.16lx is executable\n", fault);
	}

	if (!(err & MMU_ERR_PRESENT))
		printk(PRINTK_CRIT "traps: %#.16lx page was not present\n", fault);
	else if (err & MMU_ERR_WRITE_ACCESS)
		printk(PRINTK_CRIT "traps: %#.16lx page fault was caused by a write\n", fault);
	else if (err & MMU_ERR_EXEC)
		printk(PRINTK_CRIT "traps: %#.16lx page fault was caused by an instruction fetch\n", fault);

	if (err & MMU_ERR_USER)
		printk(PRINTK_CRIT "traps: %#.16lx page fault happened at CPL3\n", fault);

	if (unlikely(err & MMU_ERR_RSVD_SET_PTE))
		printk(PRINTK_CRIT "traps: %#.16lx page had an invalid bit set in PTE\n", fault);

	dump_registers(ctx);
	panic("Page fault");
}

static void exec_gp_fault(const struct context* ctx) {
	(void)ctx;
	panic("General protection fault");
}

#define X86_MAX_INSTR_SIZE 15

struct extable_entry {
	u64 fault_rip, fixup_rip;
};

extern const struct extable_entry _ld_kernel_extable_start[];
extern const struct extable_entry _ld_kernel_extable_end[];

static int try_do_fixup(struct context* ctx) {
	size_t count = _ld_kernel_extable_end - _ld_kernel_extable_start;
	for (size_t i = 0; i < count; i++) {
		const struct extable_entry* entry = &_ld_kernel_extable_start[i];
		if ((uintptr_t)ctx->rip >= entry->fault_rip &&
				(uintptr_t)ctx->rip < entry->fault_rip + X86_MAX_INSTR_SIZE) {
			ctx->rip = entry->fixup_rip;
			return 0;
		}
	}

	return -ENOENT;
}

void page_fault_isr(struct isr* isr, struct context* ctx) {
	(void)isr;
	if (current_thread()->in_usercopy) {
		int err = try_do_fixup(ctx);
		if (err == 0)
			return;
	}
	exec_page_fault(ctx);
}

void gp_fault_isr(struct isr* isr, struct context* ctx) {
	(void)isr;
	if (current_thread()->in_usercopy) {
		int err = try_do_fixup(ctx);
		if (err == 0)
			return;
	}

	exec_gp_fault(ctx);
}
