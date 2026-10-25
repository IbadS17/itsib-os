
#include "idt.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];

static struct idt_pointer idt_ptr = {
    .limit = sizeof(idt) - 1,
    .base = (uint64_t)idt
};


extern void idt_default_handler(void);
extern void idt_load(const struct idt_pointer *pointer);

static void idt_set_entry(uint8_t vector, uint64_t handler) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attributes = 0x8E;
    idt[vector].offset_middle = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_entry(i, (uint64_t)idt_default_handler);
    }

    idt_load(&idt_ptr);
}

