#pragma once

#include <lunar/types.h>
#include <arch/elf.h>

/* Elf header e_type */
#define ELF_ET_NONE 0
#define ELF_ET_REL 1
#define ELF_ET_EXEC 2
#define ELF_ET_DYN 3
#define ELF_ET_CORE 4
#define ELF_EV_NONE 0
#define ELF_EV_CURRENT 1

/* Section header sh_type */
#define ELF_SHT_NULL 0
#define ELF_SHT_PROGBITS 1
#define ELF_SHT_SYMTAB 2
#define ELF_SHT_STRTAB 3
#define ELF_SHT_RELA 4
#define ELF_SHT_HASH 5
#define ELF_SHT_DYNAMIC 6
#define ELF_SHT_NOTE 7
#define ELF_SHT_NOBITS 8
#define ELF_SHT_REL 9
#define ELF_SHT_SHLIB 10
#define ELF_SHT_DYNSYM 11
#define ELF_SHT_NUM 12

/* Section header sh_flags */
#define ELF_SHF_WRITE (1 << 0)
#define ELF_SHF_ALLOC (1 << 1)
#define ELF_SHF_EXECINSTR (1 << 2)
#define ELF_SHF_MERGE (1 << 4)
#define ELF_SHF_STRINGS (1 << 5)
#define ELF_SHF_INFO_LINK (1 << 6)
#define ELF_SHF_LINK_ORDER (1 << 7)
#define ELF_SHF_OS_NONCONFORMING (1 << 8)
#define ELF_SHF_GROUP (1 << 9)
#define ELF_SHF_TLS (1 << 10)

/* Program header p_type */
#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_PT_NOTE 4
#define ELF_PT_SHLIB 5
#define ELF_PT_PHDR 6
#define ELF_PT_TLS 7

/* Program header p_flags */
#define ELF_PF_X (1 << 0)
#define ELF_PF_W (1 << 1)
#define ELF_PF_R (1 << 2)

struct elf64_ehdr {
	unsigned char e_ident[16];
	u16 e_type;
	u16 e_machine;
	u32 e_version;
	u64 e_entry;
	u64 e_phoff;
	u64 e_shoff;
	u32 e_flags;
	u16 e_ehsize;
	u16 e_phentsize;
	u16 e_phnum;
	u16 e_shentsize;
	u16 e_shnum;
	u16 e_shstrndx;
};

struct elf64_shdr {
	u32 sh_name;
	u32 sh_type;
	u64 sh_flags;
	u64 sh_addr;
	u64 sh_offset;
	u64 sh_size;
	u32 sh_link;
	u32 sh_info;
	u64 sh_addralign;
	u64 sh_entsize;
};

struct elf64_sym {
	u32 st_name;
	u8 st_info;
	u8 st_other;
	u16 st_shndx;
	u64 st_value;
	u64 st_size;
};

struct elf64_phdr {
	u32 p_type;
	u32 p_flags;
	u64 p_offset;
	u64 p_vaddr;
	u64 p_paddr;
	u64 p_filesz;
	u64 p_memsz;
	u64 p_align;
};

static inline bool elf_validate(const struct elf64_ehdr* ehdr) {
	char magic[] = { 0x7f, 'E', 'L', 'F', '\0' };
	int cmp = __builtin_memcmp(ehdr->e_ident, magic, __builtin_strlen(magic));
	return (cmp == 0 && ehdr->e_version == ELF_EV_CURRENT && ehdr->e_machine == ARCH_ELF_EM_ARCHITECTURE);
}
