#include <lunar/compiler.h>
#include <lunar/init.h>
#include <lunar/percpu.h>
#include <lunar/sched.h>

#include <arch/tlb.h>
#include <arch/asm/linkage.h>
#include <x86_64/idt.h>
#include <x86_64/e9.h>

__diag_push();
__diag_ignore("-Wmissing-prototypes")

_Noreturn void __asmlinkage arch_x86_64_kernel_ap_main(struct arch_limine_mp_info* cpu_info) {
	arch_tlb_flush_all();
	arch_x86_64_percpu_ap_init(cpu_info);
	sched_assign_id();
	arch_x86_64_gdt_init();
	arch_x86_64_idt_init();
	kernel_ap_main();
}

_Noreturn void __asmlinkage arch_x86_64_kernel_main(void) {
	arch_x86_64_percpu_bsp_init();
	sched_assign_id();
#ifdef CONFIG_ARCH_X86_64_E9HACK
	arch_x86_64_e9_init();
#endif /* CONFIG_ARCH_X86_64_E9HACK */
	arch_x86_64_gdt_init();
	arch_x86_64_idt_init();
	kernel_main();
}

__diag_pop();
