#pragma once

#include <lunar/mm/vmm.h>
#include <lunar/mm/mm.h>
#include <lunar/asm/ctl.h>

#define PTE_COUNT 512

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
	PT_AVL_NOFREE = (1 << 9),
	PT_HUGEPAGE_PAT = (1 << 12),
	PT_NX = (1ul << 63)
};

static inline void tlb_flush_single(uintptr_t virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

static inline void tlb_flush_range(uintptr_t virtual, size_t size) {
	unsigned long count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (count >= 128) {
		ctl3_write(ctl3_read()); /* Global pages disabled, this is fine */
	} else {
		for (unsigned long i = 0; i < count; i++)
			tlb_flush_single(virtual + (PAGE_SIZE * i));
	}
}

unsigned long pagetable_mmu_to_pt(mmuflags_t mmu_flags);

void tlb_invalidate(uintptr_t address, size_t size);

/**
 * @brief Map an entry into a page table
 *
 * -EINVAL is returned if virtual or physical are not page aligned.
 * Same goes with invalid pt_flags. This value is also returned if virtual is non-canonical,
 * or if physical is NULL.
 *
 * -EEXIST is returned if the PTE is already present.
 *
 * -ENOMEM is returned if no page tables could be allocated for the new mapping
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to map, must be page aligned
 * @param physical The physical address to map the virtual address to, must be page aligned, cannot be 0
 * @param pt_flags The page table flags to use
 *
 * @return -errno on failure
 */
int pagetable_map(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, unsigned long pt_flags);

/**
 * @brief Update an entry in a page table
 *
 * -EINVAL is returned if virtual or physical are not page aligned.
 * Same goes with invalid pt_flags. This value is also returned if virtual is non-canonical,
 * or if physical is NULL.
 * 
 * -EFAULT is returned if the page sizes don't match (eg. trying to remap a 2MiB page to a 4K one)
 *
 * -ENOENT is returned if the PTE isn't present.
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to map, must be page aligned
 * @param physical The physical address to map, must be page aligned, cannot be 0
 * @param pt_flags The PT flags to use
 *
 * @return -errno on failure
 */
int pagetable_update(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, unsigned long pt_flags);

/**
 * @brief Unmap an entry in a page table
 *
 * -EINVAL is returned if virtual isn't page aligned. This value is also returned if virtual is non-canonical.
 * -ENOENT is returned if the PTE isn't present
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to unmap, must be page aligned
 */
int pagetable_unmap(pte_t* pagetable, uintptr_t virtual);

/**
 * @brief Get the physical address of a mapping in a page table
 *
 * Returns NULL if the physical address isn't mapped.
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address, does not need to be page aligned
 *
 * @return The physical address of the mapping
 */
physaddr_t pagetable_get_physical(pte_t* pagetable, uintptr_t virtual);

/**
 * @brief Get the base address of a top level page table index
 * @param index The index
 * @return The base address
 */
uintptr_t pagetable_get_base_address_from_top_index(unsigned int index);

void pagetable_init(void);
void mm_cache_init(void);
void pagetable_kmm_init(struct mm* mm_struct);

struct page_snapshot {
	uintptr_t start;
	physaddr_t physical;
	size_t len; /* multiple of page_size */
	size_t page_size;
	mmuflags_t mmu_flags;
	int vmm_flags;
	struct page_snapshot* next;
};

/**
 * @brief Save the state of pages
 *
 * @param mm_struct The context
 * @param virtual The virtual address to start saving at
 * @param size The size of the region
 *
 * @return The pointer to the saved pages, or -errno on failure
 */
struct page_snapshot* snapshot_pages(struct mm* mm_struct, uintptr_t virtual, size_t size);

/**
 * @brief Restore the state of pages
 *
 * @param mm_struct The contex t
 * @param snapshots The page snapshots
 */
void snapshot_restore_pages(struct mm* mm_struct, struct page_snapshot* snapshots);

/**
 * @brief Clean up snapshots
 *
 * @param snapshots The snapshots to clean up
 * @param free Whether or not to free the physical pages at the virtual address (only when mapped with VMM_ALLOC)
 */
void snapshot_cleanup(struct page_snapshot* snapshots, bool free);

/**
 * @brief Called when out of memory
 *
 * When the MM_NOFAIL flag is set, this function can get called when there is no memory.
 */
void out_of_memory(void);

#define __ASM_EXTABLE(fault, fixup) \
	".section .extable, \"a\"\n" \
	".balign 8\n\t" \
	".long (" #fault ")-.,(" #fixup ")-.\n\t" \
	".previous\n"
#define __read_user(ptr, instr, val) \
	({ \
		int __err; \
		typeof(*(val)) __val; \
		__asm__ volatile("1: " instr " (%2), %1\n\t" \
				"mov $0, %0\n\t" \
				"jmp 4f\n" \
				".section .fixup, \"ax\"\n\t" \
				"3: mov %3, %0\n\t" \
				"mov $0, %1\n\t" \
				"jmp 4f\n" \
				".previous\n" \
				__ASM_EXTABLE(1b, 3b) \
				"4:" \
				: "=r"(__err), "=&r"(__val) \
				: "r"(ptr), "i"(-EFAULT) \
				: "memory"); \
		*(val) = __val; \
		__err; \
	})
#define __write_user(ptr, instr, val) \
	({ \
		int __err; \
		__asm__ volatile("1: " instr " %1, (%2)\n" \
				"mov $0, %0\n" \
				"jmp 4f\n" \
				".section .fixup, \"ax\"\n" \
				"3: mov %3, %0\n" \
				"jmp 4f\n" \
				".previous\n" \
				__ASM_EXTABLE(1b, 3b) \
				"4:" \
				: "=r"(__err) \
				: "r"(val), "r"(ptr), "i"(-EFAULT) \
				: "memory"); \
		__err; \
	})

#define read_user_8(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u8) && sizeof(*val) == sizeof(u8), \
				"sizeof(*ptr) == sizeof(u8) && sizeof(*val) == sizeof(u8)"); \
		__read_user(ptr, "movb", val); \
	})
#define read_user_16(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u16) && sizeof(*val) == sizeof(u16), \
				"sizeof(*ptr) == sizeof(u16) && sizeof(*val) == sizeof(u16)"); \
		__read_user(ptr, "movw", val); \
	})
#define read_user_32(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u32) && sizeof(*val) == sizeof(u32), \
				"sizeof(*ptr) == sizeof(u32) && sizeof(*val) == sizeof(u32)"); \
		__read_user(ptr, "movl", val); \
	})
#define read_user_64(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u64) && sizeof(*val) == sizeof(u64), \
				"sizeof(*ptr) == sizeof(u64) && sizeof(*val) == sizeof(u64)"); \
		__read_user(ptr, "movq", val); \
	})

#define write_user_8(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u8), "sizeof(*ptr) == sizeof(u8)"); \
		__write_user(ptr, "movb", (u8)(val)); \
	})
#define write_user_16(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u16), "sizeof(*ptr) == sizeof(u16)"); \
		__write_user(ptr, "movw", (u16)(val)); \
	})
#define write_user_32(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u32), "sizeof(*ptr) == sizeof(u32)"); \
		__write_user(ptr, "movl", (u32)(val)); \
	})
#define write_user_64(ptr, val) \
	({ \
		static_assert(sizeof(*ptr) == sizeof(u64), "sizeof(*ptr) == sizeof(u64)"); \
		__write_user(ptr, "movq", (u64)(val)); \
	})
