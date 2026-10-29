#include "memory.h"

void *memset(void *destination, int value, size_t size)
{
    unsigned char *dst = destination;

    for (size_t i = 0; i < size; i++)
        dst[i] = (unsigned char)value;

    return destination;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *dst = destination;
    const unsigned char *src = source;

    for (size_t i = 0; i < size; i++)
        dst[i] = src[i];

    return destination;
}

int memcmp(const void *left, const void *right, size_t size)
{
    const unsigned char *a = left;
    const unsigned char *b = right;

    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }

    return 0;
}
