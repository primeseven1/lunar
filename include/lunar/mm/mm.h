#pragma once

#include <lunar/types.h>
#include <lunar/mm/vma.h>
#include <lunar/mm/vmm.h>
#include <lunar/core/mutex.h>

#define USER_SPACE_START NULL
#define USER_SPACE_USABLE_START ((void*)0x1000)
#define USER_SPACE_END ((void*)0x7FFFFFFFFFFF)
#define KERNEL_SPACE_START ((void*)0xFFFF800000000000)
#define KERNEL_SPACE_END ((void*)0xFFFFFFFFFFFFFFFF)
#define KERNEL_SPACE_N2G_START ((void*)0xFFFFFFFF80000000)

struct mm {
	pte_t* pagetable;
	struct list_head vma_list;
	mutex_t vma_lock;
	void* mmap_start, *mmap_end;
};

typedef enum {
	MM_ZONE_DMA = (1 << 0),
	MM_ZONE_DMA32 = (1 << 1),
	MM_ZONE_NORMAL = (1 << 2),
	MM_NOFAIL = (1 << 3),
	MM_ATOMIC = (1 << 4)
} mm_t;

/**
 * @brief Switch to a new MM struct
 *
 * This is not safe to call in any normal context.
 *
 * @param new The new struct to switch to
 */
void mm_switch(struct mm* new);

/**
 * @brief Initialize an mm struct
 *
 * Do NOT call this function when creating with mm_create_user(), that function
 * already calls this function.
 *
 * @param mm The mm struct to initialize
 * @param pagetable The top level page table to use
 * @param start The start of the address space
 * @param end The end of the address space
 */
void __mm_init(struct mm* mm, pte_t* pagetable, void* start, void* end);

/**
 * @brief Create a mm struct for a user process
 * @return A pointer to the new structure
 */
struct mm* mm_create_user(void);

/**
 * @brief Destroy an mm struct
 */
void mm_destroy(struct mm* mm);
