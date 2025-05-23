#include <crescent/common.h>
#include <crescent/asm/errno.h>
#include <crescent/core/limine.h>
#include <crescent/core/trace.h>
#include <crescent/core/printk.h>
#include <crescent/lib/elf.h>

extern const u8 _ld_kernel_start;
extern const u8 _ld_kernel_end;

static const struct elf64_sym* kernel_symtab = NULL;
static const char* kernel_syms_strtab = NULL;
static u32 kernel_sym_count = 0;

#define KERNEL_MIN_VIRTUAL 0xFFFFFFFF80000000

static const char* trace_kernel_symbol_name(const u8* addr) {
	if (!kernel_symtab || !kernel_syms_strtab)
		return NULL;
	if (addr < &_ld_kernel_start || addr > &_ld_kernel_end)
		return NULL;

	size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;
	addr = addr - kernel_offset;

	for (u32 i = 0; i < kernel_sym_count; i++) {
		u64 st_value = kernel_symtab[i].st_value;
		u64 st_size = kernel_symtab[i].st_size;
		if ((uintptr_t)addr >= st_value && (uintptr_t)addr < st_value + st_size)
			return kernel_syms_strtab + kernel_symtab[i].st_name;
	}

	return NULL;
}

static ssize_t trace_kernel_symbol_offset(const u8* addr) {
	if (!kernel_symtab)
		return -1;
	if (addr < &_ld_kernel_start || addr > &_ld_kernel_end)
		return -1;

	size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;
	addr = addr - kernel_offset;

	for (u32 i = 0; i < kernel_sym_count; i++) {
		u64 st_value = kernel_symtab[i].st_value;
		u64 st_size = kernel_symtab[i].st_size;
		if ((uintptr_t)addr >= st_value && (uintptr_t)addr < st_value + st_size)
			return (uintptr_t)addr - kernel_symtab[i].st_value;
	}

	return -1;
}

void dump_stack(void) {
	const void* const* stack_frame = __builtin_frame_address(0);
	const size_t kernel_offset = (uintptr_t)&_ld_kernel_start - KERNEL_MIN_VIRTUAL;

	printk(PRINTK_CRIT "Stack trace:\n");
	for (int i = 0; i < 20; i++) {
		if (!stack_frame)
			break;
		const void* ret = stack_frame[1];
		if (!ret)
			break;

		const char* name = trace_kernel_symbol_name(ret);
		if (name) {
			ssize_t offset = trace_kernel_symbol_offset(ret);
			if (likely(offset != -1))
				printk(PRINTK_CRIT "%p <%s+%#zx>\n", ret, name, (size_t)offset);
			else
				printk(PRINTK_EMERG " %p <%s+?>\n", ret, name);
		} else {
			printk(PRINTK_CRIT " %p <?+?>\n", ret);
		}

		stack_frame = stack_frame[0];
	}
	printk(PRINTK_CRIT "End stack trace: (kernel offset: %#zx)\n", kernel_offset);
}

int tracing_init(void) {
	const struct limine_executable_file_response* response = g_limine_executable_file_request.response;
	if (!response)
		return -ENOPROTOOPT;

	struct elf64_ehdr* ehdr = response->executable_file->address;
	if (!elf64_ehdr_valid(ehdr))
		return -ENOEXEC;
	struct elf64_shdr* shdr_table = (struct elf64_shdr*)((u8*)ehdr + ehdr->e_shoff);

	/* Now find the symbol table second header and the string table associated with it */
	for (u16 i = 0; i < ehdr->e_shnum; i++) {
		if (shdr_table[i].sh_type != ELF_SHT_SYMTAB)
			continue;

		/* Check to see if the string table is available for the symbol table */
		u16 strtab_index = shdr_table[i].sh_link;
		if (unlikely(shdr_table[strtab_index].sh_type != ELF_SHT_STRTAB))
			return -ENOENT;

		kernel_symtab = (struct elf64_sym*)((u8*)ehdr + shdr_table[i].sh_offset);
		kernel_syms_strtab = (char*)((u8*)ehdr + shdr_table[strtab_index].sh_offset);
		kernel_sym_count = shdr_table[i].sh_size / sizeof(struct elf64_sym);
		break;
	}

	if (!kernel_symtab)
		return -ENOENT;

	return 0;
}
