#ifndef ITSIB_VMM_H
#define ITSIB_VMM_H

#include <stdint.h>
#include <stdbool.h>

#define VMM_PAGE_SIZE 4096ULL

#define VMM_PAGE_PRESENT  (1ULL << 0)
#define VMM_PAGE_WRITABLE (1ULL << 1)
#define VMM_PAGE_USER     (1ULL << 2)
#define VMM_PAGE_HUGE     (1ULL << 7)
#define VMM_PAGE_NX       (1ULL << 63)

#define VMM_PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

/*
 * Virtual Memory Manager
 *
 * Initial stage:
 * - Inspect existing page tables.
 * - Prepare safe virtual-address mapping APIs.
 * - Preserve the mappings created by Limine.
 */

bool vmm_init(uint64_t hhdm_offset, uint64_t pml4_physical);

bool vmm_is_initialized(void);

uint64_t vmm_get_pml4_physical(void);

bool vmm_get_pte(uint64_t virtual_address, uint64_t *entry_out);
bool vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

bool vmm_unmap_page(
    uint64_t virtual_address,
    uint64_t *physical_address_out
);

bool vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address_out
);

#endif
