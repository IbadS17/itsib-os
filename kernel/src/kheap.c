#include "kheap.h"
#include "pmm.h"

#define HEAP_ALIGNMENT 16ULL
#define HEAP_MIN_SPLIT (sizeof(struct heap_block) + HEAP_ALIGNMENT)

struct heap_block {
    size_t size;
    bool free;
    struct heap_block *next;
    struct heap_block *prev;
    struct heap_block *page_start;
    uint64_t reserved;
};

static uint64_t heap_hhdm_offset = 0;
static struct heap_block *heap_head = NULL;

static size_t align_up(size_t value) {
    return (value + HEAP_ALIGNMENT - 1) &
           ~(HEAP_ALIGNMENT - 1);
}

static bool add_heap_page(void) {
    uint64_t physical_page = pmm_alloc_page();

    if (physical_page == 0) {
        return false;
    }

    struct heap_block *block =
        (struct heap_block *)(heap_hhdm_offset + physical_page);

    block->size = PAGE_SIZE - sizeof(struct heap_block);
    block->free = true;
    block->next = NULL;
    block->prev = NULL;
    block->page_start = block;
    block->reserved = 0;

    if (heap_head == NULL) {
        heap_head = block;
        return true;
    }

    struct heap_block *last = heap_head;

    while (last->next != NULL) {
        last = last->next;
    }

    last->next = block;
    block->prev = last;

    return true;
}

bool kheap_init(uint64_t hhdm_offset) {
    if (heap_head != NULL) {
        return false;
    }

    heap_hhdm_offset = hhdm_offset;

    return add_heap_page();
}

void *kmalloc(size_t size) {
    if (size == 0 || heap_head == NULL) {
        return NULL;
    }

    if (size > SIZE_MAX - (HEAP_ALIGNMENT - 1)) {
        return NULL;
    }

    size = align_up(size);

    if (size > PAGE_SIZE - sizeof(struct heap_block)) {
        return NULL;
    }

    for (;;) {
        struct heap_block *block = heap_head;

        while (block != NULL) {
            if (block->free && block->size >= size) {
                if (block->size >= size + HEAP_MIN_SPLIT) {
                    struct heap_block *split =
                        (struct heap_block *)
                        ((uint8_t *)(block + 1) + size);

                    split->size =
                        block->size - size -
                        sizeof(struct heap_block);

                    split->free = true;
                    split->next = block->next;
                    split->prev = block;
                    split->page_start = block->page_start;
                    split->reserved = 0;

                    if (split->next != NULL) {
                        split->next->prev = split;
                    }

                    block->next = split;
                    block->size = size;
                }

                block->free = false;
                return block + 1;
            }

            block = block->next;
        }

        if (!add_heap_page()) {
            return NULL;
        }
    }
}

bool kfree(void *ptr) {
    if (ptr == NULL || heap_head == NULL) {
        return false;
    }

    struct heap_block *block = heap_head;

    while (block != NULL) {
        if ((void *)(block + 1) == ptr) {
            if (block->free) {
                return false;
            }

            block->free = true;

            if (block->next != NULL &&
                block->next->free &&
                block->next->page_start == block->page_start) {
                struct heap_block *next = block->next;

                block->size += sizeof(struct heap_block) +
                               next->size;

                block->next = next->next;

                if (block->next != NULL) {
                    block->next->prev = block;
                }
            }

            if (block->prev != NULL &&
                block->prev->free &&
                block->prev->page_start == block->page_start) {
                struct heap_block *prev = block->prev;

                prev->size += sizeof(struct heap_block) +
                              block->size;

                prev->next = block->next;

                if (prev->next != NULL) {
                    prev->next->prev = prev;
                }
            }

            return true;
        }

        block = block->next;
    }

    return false;
}
