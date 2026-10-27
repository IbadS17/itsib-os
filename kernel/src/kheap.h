#ifndef ITSIB_KHEAP_H
#define ITSIB_KHEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool kheap_init(uint64_t hhdm_offset);

void *kmalloc(size_t size);

bool kfree(void *ptr);

#endif
