
#ifndef ITSIB_PMM_H
#define ITSIB_PMM_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

#define PAGE_SIZE 4096ULL

void pmm_init(
    struct limine_memmap_response *memmap,
    uint64_t hhdm_offset
);

uint64_t pmm_get_usable_pages(void);
uint64_t pmm_get_bitmap_bytes(void);
uint64_t pmm_get_free_pages(void);

uint64_t pmm_alloc_page(void);
bool pmm_free_page(uint64_t physical_address);

#endif
