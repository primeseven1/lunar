#include <lunar/elf.h>

bool elf64_header_ok(const struct elf64_ehdr* ehdr) {
	const char magic[ELF_SELFMAG] = { ELF_MAG0, ELF_MAG1, ELF_MAG2, ELF_MAG3 };
	if (__builtin_memcmp(ehdr->e_ident.mag, magic, sizeof(magic)) != 0)
		return false;

	if (ehdr->e_ident.class != ELF_CLASS_64 || ehdr->e_ident.data != ELF_DATA_2LSB ||
			(ehdr->e_ident.version != ELF_EV_CURRENT && ehdr->e_version != ELF_EV_CURRENT) ||
			ehdr->e_ident.osabi != ELF_OSABI_SYSTEMV || ehdr->e_ident.osabi_version != 0)
		return false;
	if (ehdr->e_machine != ARCH_ELF_EM_ARCHITECTURE)
		return false;
	if (ehdr->e_type != ELF_ET_EXEC || ehdr->e_phentsize != sizeof(struct elf64_phdr) || ehdr->e_phnum == 0)
		return false;

	return true;
}
