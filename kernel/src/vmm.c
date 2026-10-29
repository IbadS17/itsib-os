
#include "vmm.h"
#include "pmm.h"

#include <stddef.h>

#define VMM_MAX_PHYSICAL_ADDRESS (4ULL * 1024 * 1024 * 1024)
#define VMM_ENTRIES_PER_TABLE 512ULL

static uint64_t vmm_hhdm_offset = 0;
static uint64_t vmm_pml4_physical = 0;
static volatile uint64_t *vmm_pml4 = NULL;
static bool vmm_initialized = false;

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virtual_address) : "memory");
}

static bool is_canonical(uint64_t address) {
    uint64_t upper = address >> 48;
    bool sign_bit = ((address >> 47) & 1) != 0;

    return (!sign_bit && upper == 0) ||
           (sign_bit && upper == 0xFFFF);
}

static bool valid_physical_page(uint64_t address) {
    return address != 0 &&
           (address & (VMM_PAGE_SIZE - 1)) == 0 &&
           address < VMM_MAX_PHYSICAL_ADDRESS;
}

static volatile uint64_t *table_from_physical(uint64_t address) {
    if (!valid_physical_page(address)) {
        return NULL;
    }

    return (volatile uint64_t *)(vmm_hhdm_offset + address);
}

static uint64_t entry_physical_address(uint64_t entry) {
    return entry & VMM_PAGE_ADDRESS_MASK;
}

bool vmm_init(uint64_t hhdm_offset, uint64_t pml4_physical) {
    if (vmm_initialized || !valid_physical_page(pml4_physical)) {
        return false;
    }

    uint64_t active_pml4 = read_cr3() & VMM_PAGE_ADDRESS_MASK;

    if (active_pml4 != pml4_physical) {
        return false;
    }

    vmm_hhdm_offset = hhdm_offset;
    vmm_pml4_physical = pml4_physical;
    vmm_pml4 = table_from_physical(pml4_physical);

    if (vmm_pml4 == NULL) {
        return false;
    }

    vmm_initialized = true;
    return true;
}

bool vmm_is_initialized(void) {
    return vmm_initialized;
}

uint64_t vmm_get_pml4_physical(void) {
    return vmm_initialized ? vmm_pml4_physical : 0;
}

/*
 * Walk the existing page tables.
 * Returns the leaf entry and its page size.
 */
static bool walk_page_tables(
    uint64_t virtual_address,
    volatile uint64_t **leaf_out,
    uint64_t *page_size_out
) {
    if (!vmm_initialized || !is_canonical(virtual_address)) {
        return false;
    }

    uint64_t indices[4] = {
        (virtual_address >> 39) & 0x1FF,
        (virtual_address >> 30) & 0x1FF,
        (virtual_address >> 21) & 0x1FF,
        (virtual_address >> 12) & 0x1FF
    };

    volatile uint64_t *table = vmm_pml4;

    for (unsigned level = 0; level < 4; ++level) {
        uint64_t entry = table[indices[level]];

        if (!(entry & VMM_PAGE_PRESENT)) {
            return false;
        }

        if (level == 3) {
            *leaf_out = &table[indices[level]];
            *page_size_out = VMM_PAGE_SIZE;
            return true;
        }

        if (level == 1 && (entry & VMM_PAGE_HUGE)) {
            *leaf_out = &table[indices[level]];
            *page_size_out = 1ULL << 30;
            return true;
        }

        if (level == 2 && (entry & VMM_PAGE_HUGE)) {
            *leaf_out = &table[indices[level]];
            *page_size_out = 1ULL << 21;
            return true;
        }

        uint64_t next_physical = entry_physical_address(entry);
        table = table_from_physical(next_physical);

        if (table == NULL) {
            return false;
        }
    }

    return false;
}

bool vmm_get_pte(uint64_t virtual_address, uint64_t *entry_out) {
    if (entry_out == NULL) {
        return false;
    }

    volatile uint64_t *leaf = NULL;
    uint64_t page_size = 0;

    if (!walk_page_tables(virtual_address, &leaf, &page_size)) {
        return false;
    }

    (void)page_size;
    *entry_out = *leaf;
    return true;
}

bool vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address_out
) {
    if (physical_address_out == NULL) {
        return false;
    }

    volatile uint64_t *leaf = NULL;
    uint64_t page_size = 0;

    if (!walk_page_tables(virtual_address, &leaf, &page_size)) {
        return false;
    }

    uint64_t entry = *leaf;
    uint64_t offset = virtual_address & (page_size - 1);
    uint64_t base = entry_physical_address(entry) & ~(page_size - 1);

    *physical_address_out = base + offset;
    return true;
}

/*
 * Create missing intermediate tables.
 * This first implementation restricts table pages to
 * physical addresses accessible through our current HHDM
 * and PMM's below-4-GiB allocation range.
 */
bool vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (!vmm_initialized ||
        !is_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        !valid_physical_page(physical_address) ||
        (flags & VMM_PAGE_HUGE)) {
        return false;
    }

    uint64_t indices[4] = {
        (virtual_address >> 39) & 0x1FF,
        (virtual_address >> 30) & 0x1FF,
        (virtual_address >> 21) & 0x1FF,
        (virtual_address >> 12) & 0x1FF
    };

    volatile uint64_t *table = vmm_pml4;
    uint64_t created_tables[3];
    unsigned created_count = 0;

    for (unsigned level = 0; level < 3; ++level) {
        uint64_t entry = table[indices[level]];

        if (entry & VMM_PAGE_PRESENT) {
            if (entry & VMM_PAGE_HUGE) {
                goto rollback;
            }

            table = table_from_physical(entry_physical_address(entry));

            if (table == NULL) {
                goto rollback;
            }

            continue;
        }

        uint64_t new_physical = pmm_alloc_page();

        if (!valid_physical_page(new_physical)) {
            goto rollback;
        }

        volatile uint64_t *new_table = table_from_physical(new_physical);

        if (new_table == NULL) {
            (void)pmm_free_page(new_physical);
            goto rollback;
        }

        for (unsigned i = 0; i < VMM_ENTRIES_PER_TABLE; ++i) {
            new_table[i] = 0;
        }

        uint64_t parent_flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE;

        if (flags & VMM_PAGE_USER) {
            parent_flags |= VMM_PAGE_USER;
        }

        table[indices[level]] = new_physical | parent_flags;
        created_tables[created_count++] = new_physical;

        table = new_table;
    }

    if (table[indices[3]] & VMM_PAGE_PRESENT) {
        goto rollback;
    }

    table[indices[3]] =
        physical_address |
        VMM_PAGE_PRESENT |
        (flags & (VMM_PAGE_WRITABLE | VMM_PAGE_USER | VMM_PAGE_NX));

    invalidate_page(virtual_address);
    return true;

rollback:
    /*
     * Only remove intermediate tables created by this call,
     * and only when still empty. Never free a pre-existing table.
     */
    while (created_count > 0) {
        uint64_t created_physical = created_tables[--created_count];
        volatile uint64_t *created_table =
            table_from_physical(created_physical);

        if (created_table == NULL) {
            continue;
        }

        bool empty = true;

        for (unsigned i = 0; i < VMM_ENTRIES_PER_TABLE; ++i) {
            if (created_table[i] != 0) {
                empty = false;
                break;
            }
        }

        if (!empty) {
            continue;
        }

        /* Locate the parent again from the root. */
        volatile uint64_t *parent = vmm_pml4;

        for (unsigned level = 0; level < 3; ++level) {
            uint64_t parent_entry = parent[indices[level]];

            if (!(parent_entry & VMM_PAGE_PRESENT) ||
                (parent_entry & VMM_PAGE_HUGE)) {
                parent = NULL;
                break;
            }

            uint64_t child_physical =
                entry_physical_address(parent_entry);

            if (child_physical == created_physical) {
                parent[indices[level]] = 0;
                (void)pmm_free_page(created_physical);
                break;
            }

            parent = table_from_physical(child_physical);

            if (parent == NULL) {
                break;
            }
        }
    }

    return false;
}

bool vmm_unmap_page(
    uint64_t virtual_address,
    uint64_t *physical_address_out
) {
    if (!vmm_initialized ||
        !is_canonical(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    volatile uint64_t *leaf = NULL;
    uint64_t page_size = 0;

    if (!walk_page_tables(virtual_address, &leaf, &page_size) ||
        page_size != VMM_PAGE_SIZE) {
        return false;
    }

    uint64_t old_entry = *leaf;

    if (!(old_entry & VMM_PAGE_PRESENT)) {
        return false;
    }

    *leaf = 0;
    invalidate_page(virtual_address);

    if (physical_address_out != NULL) {
        *physical_address_out = entry_physical_address(old_entry);
    }

    /*
     * The physical page is intentionally NOT freed here:
     * ownership of mapped physical memory belongs to the caller.
     */
    return true;
}
