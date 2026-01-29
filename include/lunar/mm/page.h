#pragma once

#define PAGE_SIZE 0x1000ul
#define HUGEPAGE_2M_SIZE 0x200000ul
#define PAGE_SHIFT 12
#define HUGEPAGE_2M_SHIFT 21

typedef unsigned long pte_t;

typedef enum {
	MMU_NONE = 0, /* No access to the page */
	MMU_READ = (1 << 0), /* Page is readable */
	MMU_WRITE = (1 << 1), /* Page is writable */
	MMU_USER = (1 << 2), /* Page is a user page, don't use directly */
	MMU_WRITETHROUGH = (1 << 3), /* Page should use writethrough caching */
	MMU_CACHE_DISABLE = (1 << 4), /* No caching on the page */
	MMU_EXEC = (1 << 5) /* Page is executable */
} mmuflags_t;
