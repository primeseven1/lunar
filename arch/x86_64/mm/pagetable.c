#include <lunar/common.h>
#include <lunar/mm.h>
#include <lunar/panic.h>
#include <lunar/string.h>
#include <lunar/limine.h>
#include <lunar/init.h>
#include <lunar/proc.h>
#include <lunar/printk.h>

#include <arch/page.h>
#include <x86_64/asm/ctl.h>
#include <x86_64/asm/cpuid.h>

#define PUD_SHIFT 30
#define PUD_SIZE (1ul << PUD_SHIFT)
#define PTE_COUNT (PAGE_SIZE / sizeof(pte_t))

static struct page* alloc_table(void) {
	struct page* page = alloc_page(MM_ZONE_NORMAL);
	if (!page)
		return NULL;
	memset(page_hhdm_virtual(page), 0, PAGE_SIZE);
	return page;
}

static void free_table_physical(physaddr_t physical) {
	struct page* page;
	int err = get_page_from_address(physical, &page);
	if (unlikely(err)) {
		if (err == -EACCES)
			bug("Use after free");
		else if (unlikely(err == -ENOMEM))
			bug("Page not backed by physical memory?"); /* Should never ever happen */
		else
			panic("Unhandled error calling get_page_from_address() in function %s(): %d", __func__, err);
	}

	release_page(page); /* Release get_page_from_address() ref */
	release_page(page); /* Now release the page table ref */
}

pte_t* arch_pagetable_new(void) {
	pte_t* ret = page_hhdm_virtual(alloc_page(MM_ZONE_NORMAL));
	if (ret) {
		memcpy(ret, current_proc()->mm_struct->pagetable, PAGE_SIZE);
		memset(ret, 0, PAGE_SIZE / 2); /* Zero user page tables */
	}
	return ret;
}

enum pt_flags {
	PT_PRESENT = (1 << 0),
	PT_READ_WRITE = (1 << 1),
	PT_USER_SUPERVISOR = (1 << 2),
	PT_WRITETHROUGH = (1 << 3),
	PT_CACHE_DISABLE = (1 << 4),
	PT_ACCESSED = (1 << 5),
	PT_DIRTY = (1 << 6),
	PT_4K_PAT = (1 << 7),
	PT_HUGEPAGE = (1 << 7),
	PT_GLOBAL = (1 << 8),
	PT_HUGEPAGE_PAT = (1 << 12),
	PT_NX = (1ul << 63)
};

/* Depth is the level of the table (3 = PML4, 2 = PDPT, 1 = PD, 0 = PT), leaf frames are not freed */
static void destroy_depth(pte_t* table, int depth) {
	if (depth == 0)
		return;

	const int count = (depth == 3) ? PTE_COUNT / 2 : PTE_COUNT;
	for (int i = 0; i < count; i++) {
		const pte_t entry = table[i];
		if (!(entry & PT_PRESENT))
			continue;
		if (entry & PT_HUGEPAGE) {
			if (unlikely(depth != 1 && depth != 2))
				bug("Hugepage flag set on invalid PTE");
			continue;
		}

		const physaddr_t address = entry & ~(0xFFF | PT_NX);
		destroy_depth(hhdm_virtual(address), depth - 1);
		free_table_physical(address);
	}
}

void arch_pagetable_free(pte_t* table) {
	destroy_depth(table, 3);
	free_table_physical(hhdm_physical(table));
}

static inline pte_t* table_virtual(pte_t entry) {
	entry &= ~(0xFFF | PT_NX);
	return hhdm_virtual((physaddr_t)entry);
}

static inline bool is_virtual_canonical(uintptr_t virtual) {
	return ((virtual >> 47 == 0) || (virtual >> 47 == 0x1FFFF));
}

static inline void pagetable_get_indexes(uintptr_t virtual, unsigned int* indexes) {
	indexes[0] = virtual >> 39 & 0x01FF;
	indexes[1] = virtual >> 30 & 0x01FF;
	indexes[2] = virtual >> 21 & 0x01FF;
	indexes[3] = virtual >> 12 & 0x01FF;
}

static unsigned long pgprot_to_pt(pgprot_t prot) {
	unsigned long pt_flags = 0;
	if (prot & PGPROT_READ)
		pt_flags |= PT_PRESENT;
	if (prot & PGPROT_WRITE)
		pt_flags |= PT_READ_WRITE;
	if (prot & PGPROT_USER)
		pt_flags |= PT_USER_SUPERVISOR;
	if (!(prot & PGPROT_EXEC))
		pt_flags |= PT_NX;

	if (prot & PGPROT_PWT)
		pt_flags |= PT_WRITETHROUGH;
	else if (prot & PGPROT_PCD)
		pt_flags |= PT_CACHE_DISABLE;

	return pt_flags;
}

static int walk_pagetable(pte_t* pagetable, uintptr_t virtual, bool create, bool user, size_t* page_size, pte_t** ret) {
	*ret = NULL;

	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes);

	struct page* new_tables[3] = { NULL, NULL, NULL };
	size_t new_count = 0;
	pte_t* graft_pte = NULL;
	pte_t graft_value = 0;

	int err = 0;

	for (size_t i = 0; i < ARRAY_SIZE(indexes) - 1; i++) {
		/* Check to see if we want either a 1GiB or 2MiB page */
		if ((*page_size == PUD_SIZE && i == 1) || (*page_size == PMD_SIZE && i == 2)) {
			*ret = &pagetable[indexes[i]];
			goto out;
		}

		/* 
		 * If not present, a new page table needs to be allocated. 
		 * Otherwise, handle the case where it could be a hugepage
		 */
		if (!(pagetable[indexes[i]] & PT_PRESENT)) {
			if (!create) {
				err = -ENOENT;
				goto out;
			}
			struct page* new = alloc_table();
			if (!new) {
				err = -ENOMEM;
				goto out;
			}
			new_tables[new_count++] = new;

			pte_t value = page_to_physaddr(new) | PT_PRESENT | PT_READ_WRITE | (user ? PT_USER_SUPERVISOR : 0);
			if (graft_pte) {
				pagetable[indexes[i]] = value;
			} else {
				graft_pte = &pagetable[indexes[i]];
				graft_value = value;
			}

			pagetable = page_hhdm_virtual(new);
			continue;
		} else if (pagetable[indexes[i]] & PT_HUGEPAGE) {
			if (unlikely(i != 1 && i != 2))
				bug("Hugepage flag set on invalid PTE");

			/* 
			 * Make sure we're not returning a PTE that points to another page table, 
			 * a *page_size of zero means that the caller doesn't care if it's a hugepage or not.
			 *
			 * If *page_size is zero, then write the page size so that way the caller knows 
			 * if it needs it for some reason.
			 */
			size_t _page_size = (i == 1) ? PUD_SIZE : PMD_SIZE;
			if (_page_size != *page_size) {
				if (*page_size != 0) {
					err = -EEXIST;
					goto out;
				}
				*page_size = _page_size;
			}

			*ret = &pagetable[indexes[i]];
			goto out;
		}

		if (user && !(pagetable[indexes[i]] & PT_USER_SUPERVISOR)) {
			err = -EFAULT;
			printk(PRINTK_WARN "mm: Attempted to make user space mapping near kernel space mapping\n");
			goto out;
		}

		pagetable = table_virtual(pagetable[indexes[i]]);
	}

	*page_size = PAGE_SIZE;
	*ret = &pagetable[indexes[3]];
out:
	if (err == 0) {
		if (graft_pte)
			*graft_pte = graft_value; /* Link into the live tree all at once */
	} else {
		while (new_count--)
			release_page(new_tables[new_count]);
	}
	return err;
}

int arch_pagetable_map(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, bool hugetlb, pgprot_t prot) {
	unsigned long pt_flags = pgprot_to_pt(prot);
	if (hugetlb)
		pt_flags |= PT_HUGEPAGE;

	size_t page_size = hugetlb ? PMD_SIZE : PAGE_SIZE;
	if ((uintptr_t)virtual & (page_size - 1) || physical & (page_size - 1) || 
			!is_virtual_canonical(virtual) || !physical ||
			(prot & ~PGPROT_MASK) || (prot & PGPROT_PWT && prot & PGPROT_PCD))
		return -EINVAL;

	pte_t* pte;
	int err = walk_pagetable(pagetable, virtual, true, !!(prot & PGPROT_USER), &page_size, &pte);
	if (err)
		return err;

	if (*pte)
		return -EEXIST;

	*pte = physical | pt_flags;
	return 0;
}

int arch_pagetable_update(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, bool hugetlb, pgprot_t prot) {
	if (!is_virtual_canonical(virtual) || !physical ||
			(prot & ~PGPROT_MASK) || (prot & PGPROT_PWT && prot & PGPROT_PCD))
		return -EINVAL;

	unsigned long pt_flags = pgprot_to_pt(prot);

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, !!(prot & PGPROT_USER), &page_size, &pte);
	if (err)
		return err;

	if ((hugetlb && page_size != PMD_SIZE) ||
			(!(hugetlb) && page_size != PAGE_SIZE))
		return -EFAULT;
	if ((uintptr_t)virtual & (page_size - 1) || physical & (page_size - 1))
		return -EINVAL;

	*pte = physical | pt_flags;
	return 0;
}

/* TODO: Because this function does not free page tables, this breaks hugepage support. This will need to be resolved elsewhere, but is not a priority since the VMM does not support hugepages (yet) */
int arch_pagetable_unmap(pte_t* pagetable, uintptr_t virtual) {
	if (!is_virtual_canonical(virtual))
		return -EINVAL;

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, false, &page_size, &pte);
	if (err)
		return err;
	if ((uintptr_t)virtual & (page_size - 1))
		return -EINVAL;

	if (!(*pte))
		return -ENOENT;

	*pte = 0;
	return 0;
}

physaddr_t arch_pagetable_get_physical(pte_t* pagetable, uintptr_t virtual) {
	if (!is_virtual_canonical(virtual))
		return 0;

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, false, &page_size, &pte);
	if (err)
		return 0;

	if (!(*pte))
		return 0;

	return (*pte & ~(0xFFF | PT_NX)) + ((uintptr_t)virtual & (page_size - 1));
}

pte_t* arch_pagetable_get_cpu_current(void) {
	return hhdm_virtual(arch_x86_64_ctl3_read());
}

void arch_pagetable_switch(pte_t* pagetable) {
	arch_x86_64_ctl3_write(hhdm_physical(pagetable));
}

size_t arch_pagetable_iterate_range(pte_t* pagetable, uintptr_t virtual, uintptr_t* next) {
	if (!is_virtual_canonical(virtual)) {
		*next = KERNEL_SPACE_START;
		return 0;
	}

	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes);

	static const size_t span[4] = { 1ull << 39, 1ull << 30, 1ull << 21, 1ull << 12 };
	for (int level = 0; level < 4; level++) {
		pte_t entry = pagetable[indexes[level]];
		bool leaf = (level == 3 || ((level == 1 || level == 2) && (entry & PT_HUGEPAGE)));
		if (leaf) {
			*next = virtual + span[level];
			/*
			 * Indicates an invalid state, physical address 0 can't be mapped, and the page is non-accessible.
			 * The bootloader is able to map to physical address zero, but the loader should set it to present.
			 * If this triggers in early boot when creating the HHDM VMA ranges, something needs to change.
			 */
			bug(entry == PT_HUGEPAGE);
			return entry ? span[level] : 0;
		}
		if (!(entry & PT_PRESENT)) {
			*next = virtual + span[level];
			return 0;
		}
		pagetable = table_virtual(entry);
	}

	bug("unreachable");
}

static struct limine_paging_mode_request __limine_request paging_mode = {
	.request.id = LIMINE_PAGING_MODE_REQUEST,
	.request.revision = 1,
	.min_mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.max_mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.mode = ARCH_X86_64_LIMINE_PAGING_MODE_4LVL,
	.response = NULL
};

void arch_pagetable_init(void) {
	u32 ecx, _unused;
	arch_x86_64_cpuid(0x07, 0, &_unused, &_unused, &ecx, &_unused);

	/* bit 16 being set means the CPU supports level 5 paging */
	bool level4 = ecx & (1 << 16) ? !(arch_x86_64_ctl4_read() & ARCH_X86_64_CTL4_LA57) : true;
	bug(!level4); /* Either the wrong paging mode was selected, or something bad happened */

	/* Allocate all higher half L4 tables */
	pte_t* l4 = hhdm_virtual(arch_x86_64_ctl3_read());
	for (size_t i = PTE_COUNT / 2; i < PTE_COUNT; i++) {
		if (!(l4[i] & PT_PRESENT)) {
			struct page* page = alloc_table();
			if (!page)
				out_of_memory();
			l4[i] = page_to_physaddr(page) | PT_PRESENT | PT_READ_WRITE;
		}
	}

	/* Page table changed, flush just in case */
	arch_x86_64_ctl3_write(arch_x86_64_ctl3_read());
}
