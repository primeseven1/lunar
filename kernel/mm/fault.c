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

static void exec_page_fault(void* fault, int err) {
	struct vma* vma = vma_find(current_thread()->proc->mm_struct, fault);
	if (!vma) {
		printk(PRINTK_CRIT "traps: No VMA found at %p\n", fault);
		return;
	}

	if (vma->flags & MMU_READ)
		printk(PRINTK_CRIT "traps: %p is readable\n", fault);
	if (vma->flags & MMU_WRITE)
		printk(PRINTK_CRIT "traps: %p is writable\n", fault);
	if (vma->flags & MMU_EXEC)
		printk(PRINTK_CRIT "traps: %p is executable\n", fault);

	if (!(err & MMU_ERR_PRESENT))
		printk(PRINTK_CRIT "traps: %p page was not present\n", fault);
	else if (err & MMU_ERR_WRITE_ACCESS)
		printk(PRINTK_CRIT "traps: %p page fault was caused by a write\n", fault);
	else if (err & MMU_ERR_EXEC)
		printk(PRINTK_CRIT "traps: %p page fault was caused by an instruction fetch\n", fault);

	if (err & MMU_ERR_USER)
		printk(PRINTK_CRIT "traps: %p page fault happened at CPL3\n", fault);

	if (unlikely(err & MMU_ERR_RSVD_SET_PTE))
		printk(PRINTK_CRIT "traps: %p page had an invalid bit set in PTE\n", fault);
}

void page_fault_isr(struct isr* isr, struct context* ctx) {
	(void)isr;

	if (ctx->cs == SEGMENT_KERNEL_CODE) {
		if (!ctx->cr2)
			panic("NULL pointer dereference at rip: %p", ctx->rip);
		exec_page_fault(ctx->cr2, ctx->err_code);
		dump_registers(ctx);
		panic("kernel page fault");
	}
}
