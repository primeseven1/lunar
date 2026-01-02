#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/vfs.h>

enum elf_ehdr_types {
	ELF_ET_NONE = 0,
	ELF_ET_REL = 1,
	ELF_ET_EXEC = 2,
	ELF_ET_DYN = 3,
	ELF_ET_CORE = 4
};

enum elf_ehdr_machines {
	ELF_EM_NONE = 0,
	ELF_EM_X86_64 = 62
};

enum elf_ehdr_versions {
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

enum elf_shdr_types {
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

enum elf_shdr_flags {
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

enum elf_sym_bindings {
	ELF_STB_LOCAL,
	ELF_STB_GLOBAL,
	ELF_STB_WEAK
};

enum elf_sym_types {
	ELF_STT_NOTYPE,
	ELF_STT_OBJECT,
	ELF_STT_FUNC,
	ELF_STT_SECTION,
	ELF_STT_FILE,
	ELF_STT_COMMON,
	ELF_STT_TLS
};

enum elf_sym_visibility {
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

enum elf_phdr_types {
	ELF_PT_NULL,
	ELF_PT_LOAD,
	ELF_PT_DYNAMIC,
	ELF_PT_INTERP,
	ELF_PT_NOT,
	ELF_PT_SHLIB,
	ELF_PT_PHDR,
	ELF_PT_TLS,
	ELF_PT_NUM,
	ELF_PT_LOOS = 0x60000000,
	ELF_PT_HIOS = 0x6fffffff,
	ELF_PT_LOPROC = 0x70000000,
	ELF_PT_HIPROC = 0x7fffffff 
};

enum elf_phdr_flags {
	ELF_PF_X = (1 << 0),
	ELF_PF_W = (1 << 1),
	ELF_PF_R = (1 << 2),
	ELF_PF_MASKOS = 0x0ff00000,
	ELF_PF_MASKPROC = 0xf0000000
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

enum elf64_reloc_types {
	ELF_R_X86_64_NONE,
	ELF_R_X86_64_64,
	ELF_R_X86_64_PC32,
	ELF_R_X86_64_GOT32,
	ELF_R_X86_64_PLT32,
	ELF_R_X86_64_COPY,
	ELF_R_X86_64_GLOB_DAT,
	ELF_R_X86_64_JUMP_SLOT,
	ELF_R_X86_64_RELATIVE,
	ELF_R_X86_64_GOTPCREL,
	ELF_R_X86_64_32,
	ELF_R_X86_64_32S,
	ELF_R_X86_64_16,
	ELF_R_X86_64_PC16,
	ELF_R_X86_64_8,
	ELF_R_X86_64_PC8,
	ELF_R_X86_64_DTPMOD64,
	ELF_R_X86_64_DTPOFF64,
	ELF_R_X86_64_TPOFF64,
	ELF_R_X86_64_TLSGD,
	ELF_R_X86_64_TLSLD,
	ELF_R_X86_64_DTPOFF32,
	ELF_R_X86_64_GOTTPOFF,
	ELF_R_X86_64_TPOFF32,
	ELF_R_X86_64_NUM
};

#define ELF64_R_SYM(x) ((x) >> 32)
#define ELF64_R_TYPE(x) ((x) & 0xffffffff)
#define ELF64_R_INFO(sym, type) ((((u64)(sym)) << 32) + (type))

struct elf64_rel {
	u64 r_offset;
	u64 r_info;
};

struct elf64_rela {
	u64 r_offset;
	u64 r_info;
	i64 r_addend;
};

bool elf_validate(struct elf64_ehdr* ehdr);
int elf_load(struct vnode* vnode);
