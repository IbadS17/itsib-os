
#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3] = {
    {0},                              // Null descriptor
    {0, 0, 0, 0x9A, 0x20, 0},        // 64-bit code descriptor
    {0, 0, 0, 0x92, 0x00, 0}         // Data descriptor
};

static struct gdt_pointer gdt_ptr = {
    .limit = sizeof(gdt) - 1,
    .base = (uint64_t)gdt
};

extern void gdt_flush(const struct gdt_pointer *pointer);

void gdt_init(void) {
    gdt_flush(&gdt_ptr);
}