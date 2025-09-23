#pragma once

#include <lunar/types.h>

enum elf64_ehdr_types {
	ELF_ET_NONE = 0,
	ELF_ET_REL = 1,
	ELF_ET_EXEC = 2,
	ELF_ET_DYN = 3,
	ELF_ET_CORE = 4
};

enum elf64_ehdr_machines {
	ELF_EM_NONE = 0,
	ELF_EM_X86_64 = 62
};

enum elf64_ehdr_versions {
	ELF_EV_NONE = 0,
	ELF_EV_CURRENT = 1
};

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

static inline bool elf64_ehdr_valid(struct elf64_ehdr* ehdr) {
	char magic[] = { 0x7f, 'E', 'L', 'F', '\0' };
	int cmp = __builtin_memcmp(ehdr->e_ident, magic, __builtin_strlen(magic));
	return (cmp == 0 && ehdr->e_version == ELF_EV_CURRENT);
}

static inline bool elf64_ehdr_is_arch_native(struct elf64_ehdr* ehdr) {
	return ehdr->e_machine == ELF_EM_X86_64;
}

enum elf64_shdr_types {
	ELF_SHT_NULL,
	ELF_SHT_PROGBITS,
	ELF_SHT_SYMTAB,
	ELF_SHT_STRTAB,
	ELF_SHT_RELA,
	ELF_SHT_HASH,
	ELF_SHT_DYNAMIC,
	ELF_SHT_NOTE,
	ELF_SHT_NOBITS,
	ELF_SHT_REL,
	ELF_SHT_SHLIB,
	ELF_SHT_DYNSYM,
	ELF_SHT_INIT_ARRAY = 14,
	ELF_SHT_FINI_ARRAY,
	ELF_SHT_PREINIT_ARRAY,
	ELF_SHT_GROUP,
	ELF_SHT_SYMTAB_SHNDX
};

enum elf64_shdr_flags {
	ELF_SHF_WRITE = (1 << 0),
	ELF_SHF_ALLOC = (1 << 1),
	ELF_SHF_EXECINSTR = (1 << 2),
	ELF_SHF_MERGE = (1 << 4),
	ELF_SHF_STRINGS = (1 << 5),
	ELF_SHF_INFO_LINK = (1 << 6),
	ELF_SHF_LINK_ORDER = (1 << 7),
	ELF_SHF_OS_NONCONFORMING = (1 << 8),
	ELF_SHF_GROUP = (1 << 9),
	ELF_SHF_TLS = (1 << 10)
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

enum elf64_sym_bindings {
	ELF_STB_LOCAL,
	ELF_STB_GLOBAL,
	ELF_STB_WEAK
};

enum elf64_sym_types {
	ELF_STT_NOTYPE,
	ELF_STT_OBJECT,
	ELF_STT_FUNC,
	ELF_STT_SECTION,
	ELF_STT_FILE,
	ELF_STT_COMMON,
	ELF_STT_TLS
};

enum elf64_sym_visibility {
	ELF_STV_DEFAULT,
	ELF_STV_INTERNAL,
	ELF_STV_HIDDEN,
	ELF_STV_PROTECTED
};

struct elf64_sym {
	u32 st_name;
	u8 st_info;
	u8 st_other;
	u16 st_shndx;
	u64 st_value;
	u64 st_size;
};
