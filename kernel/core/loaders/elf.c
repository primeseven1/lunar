#include <lunar/core/loaders/elf.h>

bool elf_validate(struct elf64_ehdr* ehdr) {
	char magic[] = { 0x7f, 'E', 'L', 'F', '\0' };
	int cmp = __builtin_memcmp(ehdr->e_ident, magic, __builtin_strlen(magic));
	return (cmp == 0 && ehdr->e_version == ELF_EV_CURRENT && ehdr->e_machine == ELF_EM_X86_64);
}

int elf_load(struct vnode* vnode) {
	struct elf64_ehdr ehdr;
	ssize_t count = vfs_read(vnode, &ehdr, sizeof(struct elf64_ehdr), 0, 0);
	if (count != sizeof(ehdr))
		return count < 0 ? count : -ENOEXEC;
	if (!elf_validate(&ehdr))
		return -ENOEXEC;

	return -ENOSYS;
}
