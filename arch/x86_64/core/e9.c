#ifdef CONFIG_ARCH_X86_64_E9HACK

#include <lunar/string.h>

#include <x86_64/e9.h>
#include <x86_64/pmio.h>
#include <x86_64/asm/cpuid.h>

static void e9_printk_hook(int level, int global_level, const char* msg) {
	(void)level;
	(void)global_level;
	while (*msg)
		arch_x86_64_outb(0xe9, *msg++);
}

void arch_x86_64_e9_init(void) {
	u32 _unused, ebx, ecx, edx;

	/* First check for a hypervisor */
	arch_x86_64_cpuid(CPUID_LEAF_FEATURE_BITS, 0, &_unused, &_unused, &ecx, &_unused);
	if (!(ecx & (1 << 31)))
		return;

	/* Check for QEMU specifically, E9 is also supported in bochs, but I don't know the vendor string for that */
	char hypervisor_name[13];
	arch_x86_64_cpuid(0x40000000, 0, &_unused, &ebx, &ecx, &edx);
	__builtin_memcpy(hypervisor_name, &ebx, sizeof(char) * 4);
	__builtin_memcpy(hypervisor_name + 4, &ecx, sizeof(char) * 4);
	__builtin_memcpy(hypervisor_name + 8, &edx, sizeof(char) * 4);
	hypervisor_name[12] = '\0';
	if (strcmp(hypervisor_name, "TCGTCGTCGTCG") != 0)
		return;

	/* We don't really care if this fails or not */
	int err = printk_set_hook(e9_printk_hook);
	if (err)
		printk(PRINTK_WARN "e9: Failed to set hook: %i\n", err);
}

#endif /* CONFIG_ARCH_X86_64_E9HACK */
