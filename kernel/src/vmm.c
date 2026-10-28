
#include "vmm.h"
#include "pmm.h"

#include <stddef.h>

static uint64_t vmm_hhdm_offset = 0;
static uint64_t vmm_pml4_physical = 0;
static volatile uint64_t *vmm_pml4 = NULL;
static bool vmm_initialized = false;

static inline uint64_t read_cr3(void) {
    uint64_t value;

    __asm__ volatile ("mov %%cr3, %0"
                      : "=r"(value));

    return value;
}

static bool physical_address_is_valid(uint64_t address) {
    return address != 0 &&
           (address & (VMM_PAGE_SIZE - 1)) == 0 &&
           address < (4ULL * 1024 * 1024 * 1024);
}

bool vmm_init(uint64_t hhdm_offset, uint64_t pml4_physical) {
    if (vmm_initialized || pml4_physical == 0) {
        return false;
    }

    if ((pml4_physical & (VMM_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    /*
     * This initial implementation inspects the active
     * page tables only. It does not switch CR3.
     */
    uint64_t active_pml4 = read_cr3() & VMM_PAGE_ADDRESS_MASK;

    if (active_pml4 != pml4_physical) {
        return false;
    }

    vmm_hhdm_offset = hhdm_offset;
    vmm_pml4_physical = pml4_physical;

    vmm_pml4 = (volatile uint64_t *)
        (hhdm_offset + pml4_physical);

    vmm_initialized = true;
    return true;
}

bool vmm_is_initialized(void) {
    return vmm_initialized;
}

uint64_t vmm_get_pml4_physical(void) {
    if (!vmm_initialized) {
        return 0;
    }

    return vmm_pml4_physical;
}

bool vmm_get_pte(uint64_t virtual_address, uint64_t *entry_out) {
    if (!vmm_initialized || entry_out == NULL) {
        return false;
    }

    /*
     * Only canonical x86-64 addresses are accepted
     * with four-level paging.
     */
    uint64_t upper = virtual_address >> 48;
    bool sign_bit = ((virtual_address >> 47) & 1) != 0;

    if ((!sign_bit && upper != 0) ||
        (sign_bit && upper != 0xFFFF)) {
        return false;
    }

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index   = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index   = (virtual_address >> 12) & 0x1FF;

    uint64_t entry = vmm_pml4[pml4_index];

    if ((entry & VMM_PAGE_PRESENT) == 0) {
        return false;
    }

    uint64_t table_physical = entry & VMM_PAGE_ADDRESS_MASK;

    if (!physical_address_is_valid(table_physical)) {
        return false;
    }

    volatile uint64_t *pdpt = (volatile uint64_t *)
        (vmm_hhdm_offset + table_physical);

    entry = pdpt[pdpt_index];

    if ((entry & VMM_PAGE_PRESENT) == 0) {
        return false;
    }

    if (entry & VMM_PAGE_HUGE) {
        *entry_out = entry;
        return true;
    }

    table_physical = entry & VMM_PAGE_ADDRESS_MASK;

    if (!physical_address_is_valid(table_physical)) {
        return false;
    }

    volatile uint64_t *pd = (volatile uint64_t *)
        (vmm_hhdm_offset + table_physical);

    entry = pd[pd_index];

    if ((entry & VMM_PAGE_PRESENT) == 0) {
        return false;
    }

    if (entry & VMM_PAGE_HUGE) {
        *entry_out = entry;
        return true;
    }

    table_physical = entry & VMM_PAGE_ADDRESS_MASK;

    if (!physical_address_is_valid(table_physical)) {
        return false;
    }

    volatile uint64_t *pt = (volatile uint64_t *)
        (vmm_hhdm_offset + table_physical);

    entry = pt[pt_index];

    if ((entry & VMM_PAGE_PRESENT) == 0) {
        return false;
    }

    *entry_out = entry;
    return true;
}
