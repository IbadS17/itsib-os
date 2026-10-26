
#include "pmm.h"

#define MAX_PHYSICAL_ADDRESS (4ULL * 1024 * 1024 * 1024)
#define MAX_PHYSICAL_PAGES (MAX_PHYSICAL_ADDRESS / PAGE_SIZE)
#define BITMAP_SIZE (MAX_PHYSICAL_PAGES / 8)

static uint8_t usable_bitmap[BITMAP_SIZE];
static uint8_t allocated_bitmap[BITMAP_SIZE];

static uint64_t total_usable_pages = 0;
static uint64_t free_pages = 0;
static uint64_t bitmap_bytes_required = 2 * BITMAP_SIZE;
static uint64_t next_search_index = 1;

static bool bitmap_get(uint8_t *bitmap, uint64_t index) {
    return (bitmap[index / 8] &
            (uint8_t)(1U << (index % 8))) != 0;
}

static void bitmap_set(uint8_t *bitmap, uint64_t index) {
    bitmap[index / 8] |=
        (uint8_t)(1U << (index % 8));
}

static void bitmap_clear(uint8_t *bitmap, uint64_t index) {
    bitmap[index / 8] &=
        (uint8_t)~(1U << (index % 8));
}

void pmm_init(
    struct limine_memmap_response *memmap,
    uint64_t hhdm_offset
) {
    (void)hhdm_offset;

    total_usable_pages = 0;
    free_pages = 0;
    next_search_index = 1;

    for (uint64_t i = 0; i < BITMAP_SIZE; i++) {
        usable_bitmap[i] = 0;
        allocated_bitmap[i] = 0;
    }

    if (!memmap) {
        return;
    }

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start = entry->base;
        uint64_t end = start + entry->length;

        if (end < start || start >= MAX_PHYSICAL_ADDRESS) {
            continue;
        }

        if (end > MAX_PHYSICAL_ADDRESS) {
            end = MAX_PHYSICAL_ADDRESS;
        }

        uint64_t remainder = start % PAGE_SIZE;
        if (remainder != 0) {
            uint64_t adjustment = PAGE_SIZE - remainder;

            if (start > UINT64_MAX - adjustment) {
                continue;
            }

            start += adjustment;
        }

        end -= end % PAGE_SIZE;

        if (end <= start) {
            continue;
        }

        for (uint64_t address = start; address < end;
             address += PAGE_SIZE) {
            uint64_t index = address / PAGE_SIZE;

            if (!bitmap_get(usable_bitmap, index)) {
                bitmap_set(usable_bitmap, index);
                total_usable_pages++;
                free_pages++;
            }
        }
    }
}

uint64_t pmm_get_usable_pages(void) {
    return total_usable_pages;
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

uint64_t pmm_get_bitmap_bytes(void) {
    return bitmap_bytes_required;
}

uint64_t pmm_alloc_page(void) {
    if (free_pages == 0) {
        return 0;
    }

    for (uint64_t index = next_search_index;
         index < MAX_PHYSICAL_PAGES;
         index++) {

        if (bitmap_get(usable_bitmap, index) &&
            !bitmap_get(allocated_bitmap, index)) {

            bitmap_set(allocated_bitmap, index);
            free_pages--;

            next_search_index = index + 1;
            if (next_search_index >= MAX_PHYSICAL_PAGES) {
                next_search_index = 1;
            }

            return index * PAGE_SIZE;
        }
    }

    for (uint64_t index = 1;
         index < next_search_index;
         index++) {

        if (bitmap_get(usable_bitmap, index) &&
            !bitmap_get(allocated_bitmap, index)) {

            bitmap_set(allocated_bitmap, index);
            free_pages--;

            next_search_index = index + 1;
            if (next_search_index >= MAX_PHYSICAL_PAGES) {
                next_search_index = 1;
            }

            return index * PAGE_SIZE;
        }
    }

    return 0;
}
bool pmm_free_page(uint64_t physical_address) {
    if (physical_address == 0 ||
        physical_address % PAGE_SIZE != 0 ||
        physical_address >= MAX_PHYSICAL_ADDRESS) {
        return false;
    }

    uint64_t index = physical_address / PAGE_SIZE;

    if (!bitmap_get(usable_bitmap, index) ||
        !bitmap_get(allocated_bitmap, index)) {
        return false;
    }

    bitmap_clear(allocated_bitmap, index);
    free_pages++;

    return true;
}
    