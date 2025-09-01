#pragma once

#include <crescent/types.h>

/**
 * @brief Get a physical address from a virtual address with HHDM
 * @param virtual The virtual address to convert
 * @return The physical address
 */
physaddr_t hhdm_physical(const void* virtual);

/**
 * @brief Get a virtual address from a physical address with HHDM
 *
 * The address returned is not guarunteed to be mapped to virtual memory. 
 * The protocol specifies that HHDM does not compromise any unusable memory regions.
 *
 * @param physical The physical address to convert
 * @return The virtual address
 */
void* hhdm_virtual(physaddr_t physical);
