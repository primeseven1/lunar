#include <lunar/sched.h>
#include <lunar/proc.h>
#include <lunar/printk.h>

__diag_ignore("-Wmissing-prototypes");

__asmlinkage long syscall_invalid(struct arch_context* ctx) {
	struct proc* proc = current_proc();
	printk(PRINTK_ERR "syscall: PID %u made an invalid system call (RIP: %#lx)\n", proc->pid, ARCH_CONTEXT_IP(ctx));
	return -ENOSYS;
}
