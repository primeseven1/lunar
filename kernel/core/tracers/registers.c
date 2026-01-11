#include <lunar/core/trace.h>
#include <lunar/core/printk.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/msr.h>

void dump_registers(const struct context* ctx) {
	printk(PRINTK_CRIT "Register dump:\n");
	
	unsigned long cr0 = ctl0_read();
	void* cr2 = ctl2_read();
	physaddr_t cr3 = ctl3_read();
	unsigned long cr4 = ctl4_read();
	printk(PRINTK_CRIT " CR0: %#lx, CR2: %p, CR3: %#lx, CR4: %#lx\n", cr0, cr2, cr3, cr4);

	printk(PRINTK_CRIT " RAX: %#lx RBX: %#lx RCX: %#lx, RDX: %#lx\n", 
			ctx->rax, ctx->rbx, ctx->rcx, ctx->rdx);
	printk(PRINTK_CRIT " RSI: %#lx, RDI: %#lx, RBP: %#lx, RSP: %#lx\n",
			ctx->rsi, ctx->rdi, ctx->rbp, ctx->rsp);
	printk(PRINTK_CRIT " R8: %#lx, R9: %#lx, R10: %#lx, R11: %#lx\n",
			ctx->r8, ctx->r9, ctx->r10, ctx->r11);
	printk(PRINTK_CRIT " R12: %#lx, R13: %#lx, R14: %#lx, R15: %#lx\n",
			ctx->r12, ctx->r13, ctx->r14, ctx->r15);
	printk(PRINTK_CRIT " RIP: %#lx, RFLAGS: %#lx\n", ctx->rip, ctx->rflags);

	u64 efer = rdmsr(MSR_EFER);
	u64 gsbase = rdmsr(MSR_GS_BASE);
	printk(PRINTK_CRIT " EFER: %#lx, GSBASE: %p\n", efer, (void*)gsbase);
}
