#ifndef ITSIB_MEMORY_H
#define ITSIB_MEMORY_H

#include <stddef.h>

void *memset(void *destination, int value, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
int memcmp(const void *left, const void *right, size_t size);

#endif
