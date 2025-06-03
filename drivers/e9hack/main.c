#include <crescent/asm/errno.h>
#include <crescent/core/module.h>
#include <crescent/lib/string.h>
#include <crescent/asm/cpuid.h>
#include "e9.h"

static int e9hack_init(void) {
	u32 _unused, ebx, ecx, edx;

	/* First check for a hypervisor */
	cpuid(CPUID_LEAF_FEATURE_BITS, 0, &_unused, &_unused, &ecx, &_unused);
	if (!(ecx & (1 << 31)))
		return -ENODEV;

	char hypervisor_name[13];
	cpuid(0x40000000, 0, &_unused, &ebx, &ecx, &edx);
	__builtin_memcpy(hypervisor_name, &ebx, sizeof(u32));
	__builtin_memcpy(hypervisor_name + 4, &ecx, sizeof(u32));
	__builtin_memcpy(hypervisor_name + 8, &edx, sizeof(u32));
	hypervisor_name[12] = '\0';

	/* 
	 * Check for QEMU, E9 is also supported in bochs, 
	 * but I don't know the vendor string for that 
	 */
	if (strcmp(hypervisor_name, "TCGTCGTCGTCG") != 0)
		return -ENODEV;

	/* 
	 * There is no way to check if the -debugcon option was used 
	 * in qemu or not, so just assume it was used 
	 */
	return printk_set_hook(e9hack_printk_hook);
}

MODULE("e9hack", INIT_STATUS_NOTHING, e9hack_init);
