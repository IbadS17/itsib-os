#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <stdbool.h>
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "kheap.h"

__attribute__((used, section(".limine_requests_start_marker")))
static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(0);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests_end_marker")))
static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void serial_init(void) {
    outb(0x3F9, 0);
    outb(0x3FB, 0x80);
    outb(0x3F8, 1);
    outb(0x3F9, 0);
    outb(0x3FB, 3);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}


static bool serial_write(const char *s) {
    while (*s) {
        bool ready = false;

        for (unsigned int i = 0; i < 100000; i++) {
            uint8_t status;

            __asm__ volatile ("inb %1, %0"
                : "=a"(status)
                : "Nd"((uint16_t)0x3FD));

            if (status & 0x20) {
                ready = true;
                break;
            }
        }

        if (!ready) {
            return false;
        }

        outb(0x3F8, (uint8_t)*s++);
    }

    return true;
}

static void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[16];

    for (int i = 15; i >= 0; i--) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }

    for (int i = 0; i < 16; i++) {
        char character[2] = {buffer[i], '\0'};
        if (!serial_write(character)) {
            return;
        }
    }
}

static void kernel_panic(const char *message) {
    (void)serial_write("\r\nKERNEL PANIC: ");
    (void)serial_write(message);
    (void)serial_write("\r\nSystem halted.\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
static void kernel_assert(bool condition, const char *message) {
    if (!condition) {
        kernel_panic(message);
    }
}

static void pixel(struct limine_framebuffer *fb,
                  uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb->width || y >= fb->height || fb->bpp != 32)
        return;

    volatile uint32_t *p = (volatile uint32_t *)
        ((uintptr_t)fb->address + y * fb->pitch + x * 4);

    uint32_t r = (color >> 16) & 0xff;
    uint32_t g = (color >> 8) & 0xff;
    uint32_t b = color & 0xff;

    *p = ((r >> (8 - fb->red_mask_size)) << fb->red_mask_shift) |
         ((g >> (8 - fb->green_mask_size)) << fb->green_mask_shift) |
         ((b >> (8 - fb->blue_mask_size)) << fb->blue_mask_shift);
}

static void fill(struct limine_framebuffer *fb, uint32_t color) {
    for (uint64_t y = 0; y < fb->height; y++)
        for (uint64_t x = 0; x < fb->width; x++)
            pixel(fb, x, y, color);
}

static const uint8_t font[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

static void draw_text(struct limine_framebuffer *fb,
                      uint64_t x, uint64_t y,
                      const char *s, uint32_t color, unsigned scale) {
    while (*s) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c -= 32;

        if (c >= 'A' && c <= 'Z') {
            const uint8_t *glyph = font[c - 'A'];
            for (unsigned row = 0; row < 7; row++)
                for (unsigned col = 0; col < 5; col++)
                    if (glyph[row] & (1u << (4 - col)))
                        for (unsigned dy = 0; dy < scale; dy++)
                            for (unsigned dx = 0; dx < scale; dx++)
                                pixel(fb, x + col * scale + dx,
                                      y + row * scale + dy, color);
        } else if (c == ' ') {
            /* Blank space */
        }

        x += 6 * scale;
    }
}

void itsib_main(void) {
    serial_init();

    struct limine_hhdm_response *hhdm = hhdm_request.response;

    if (!hhdm) {
        kernel_panic("HHDM response unavailable");
    }

    struct limine_executable_address_response *kernel_address =
        executable_address_request.response;

    if (!kernel_address) {
        kernel_panic("Kernel address response unavailable");
    }

    (void)serial_write("Kernel physical base: 0x");
    serial_write_hex(kernel_address->physical_base);
    (void)serial_write("\r\n");

    (void)serial_write("Kernel virtual base: 0x");
    serial_write_hex(kernel_address->virtual_base);
    (void)serial_write("\r\n");


    (void)serial_write("HHDM offset: 0x");
    serial_write_hex(hhdm->offset);
    (void)serial_write("\r\n");

    gdt_init();
    idt_init();

    struct limine_memmap_response *memmap =
        memmap_request.response;

    if (!memmap) {
        (void)serial_write("ERROR: Limine provided no memory map.\r\n");
        kernel_panic("Memory map unavailable");
    }
    pmm_init(memmap, hhdm->offset);
    uint64_t initial_free_pages = pmm_get_free_pages();

    kernel_assert(initial_free_pages == pmm_get_usable_pages(),
                  "PMM test: initial free-page count is incorrect");
    (void)serial_write("PMM: Running allocator tests...\r\n");

    uint64_t page_a = pmm_alloc_page();
    uint64_t page_b = pmm_alloc_page();

    kernel_assert(page_a != 0, "PMM test: allocation A failed");
    kernel_assert(page_b != 0, "PMM test: allocation B failed");

    kernel_assert(page_a % PAGE_SIZE == 0,
                  "PMM test: page A is not aligned");
    kernel_assert(page_b % PAGE_SIZE == 0,
                  "PMM test: page B is not aligned");

    kernel_assert(page_a != page_b,
                  "PMM test: duplicate physical page allocated");

    kernel_assert(pmm_free_page(page_a),
                  "PMM test: freeing page A failed");

    kernel_assert(!pmm_free_page(page_a),
                  "PMM test: double-free was accepted");

    uint64_t page_c = pmm_alloc_page();

    kernel_assert(page_c != 0,
                "PMM test: allocation after freeing failed");

    kernel_assert(page_c != page_b,
                "PMM test: allocated page B twice");

    kernel_assert(pmm_free_page(page_c),
                "PMM test: freeing page C failed");

    uint64_t page_d = pmm_alloc_page();

    kernel_assert(page_d != 0,
                "PMM test: reallocation after freeing C failed");

    kernel_assert(pmm_free_page(page_d),
                "PMM test: freeing page D failed");

    kernel_assert(pmm_free_page(page_b),
                "PMM test: freeing page B failed");

    kernel_assert(!pmm_free_page(0),
                "PMM test: zero address was freed");

    kernel_assert(!pmm_free_page(1),
                "PMM test: unaligned address was freed");

    kernel_assert(!pmm_free_page(0x800000),
                "PMM test: reserved address was freed");

    (void)serial_write("PMM: Allocation/free tests passed!\r\n");
    (void)serial_write("PMM: Invalid-free tests passed!\r\n");
    kernel_assert(pmm_get_free_pages() == initial_free_pages,
                "PMM test: free-page count did not recover");

    kernel_assert(pmm_get_free_pages() <= pmm_get_usable_pages(),
                "PMM test: free-page count exceeds usable pages");

    (void)serial_write("PMM: Free-page accounting tests passed!\r\n");
    uint64_t hhdm_test_page = pmm_alloc_page();

    kernel_assert(hhdm_test_page != 0,
                  "HHDM test: page allocation failed");

    volatile uint64_t *test_address =
        (volatile uint64_t *)(hhdm->offset + hhdm_test_page);

    *test_address = 0x49545349424F5355ULL;

    kernel_assert(*test_address == 0x49545349424F5355ULL,
                  "HHDM test: memory read/write failed");

    kernel_assert(pmm_free_page(hhdm_test_page),
                  "HHDM test: freeing page failed");

    (void)serial_write("PMM: HHDM page read/write test passed!\r\n");
    (void)serial_write("KHEAP: Initializing kernel heap...\r\n");

    kernel_assert(kheap_init(hhdm->offset),
                  "KHEAP test: initialization failed");

    void *heap_a = kmalloc(24);
    void *heap_b = kmalloc(128);

    kernel_assert(heap_a != NULL,
                  "KHEAP test: allocation A failed");

    kernel_assert(heap_b != NULL,
                  "KHEAP test: allocation B failed");

    kernel_assert(((uintptr_t)heap_a % 16) == 0,
                  "KHEAP test: allocation A is not aligned");

    kernel_assert(((uintptr_t)heap_b % 16) == 0,
                  "KHEAP test: allocation B is not aligned");

    *(uint64_t *)heap_a = 0x4954534942414144ULL;
    *(uint64_t *)heap_b = 0x4B45524E454C4845ULL;

    kernel_assert(*(uint64_t *)heap_a == 0x4954534942414144ULL,
                  "KHEAP test: allocation A read/write failed");

    kernel_assert(*(uint64_t *)heap_b == 0x4B45524E454C4845ULL,
                  "KHEAP test: allocation B read/write failed");

    kernel_assert(kfree(heap_a),
                  "KHEAP test: freeing allocation A failed");

    kernel_assert(!kfree(heap_a),
                  "KHEAP test: double-free was accepted");

    kernel_assert(kfree(heap_b),
                  "KHEAP test: freeing allocation B failed");

    kernel_assert(!kfree((void *)(uintptr_t)0x1234),
                  "KHEAP test: invalid pointer was accepted");

    kernel_assert(kmalloc(0) == NULL,
                  "KHEAP test: zero-size allocation was accepted");

    kernel_assert(kmalloc(PAGE_SIZE) == NULL,
                  "KHEAP test: oversized allocation was accepted");

    (void)serial_write("KHEAP: Initialization test passed!\r\n");
    (void)serial_write("KHEAP: Allocation and alignment tests passed!\r\n");
    (void)serial_write("KHEAP: Free and invalid-pointer tests passed!\r\n");

    (void)serial_write("PMM: Usable 4 KiB pages: ");
    serial_write_hex(pmm_get_usable_pages());
    (void)serial_write("\r\n");

    (void)serial_write("PMM: Bitmap storage required (bytes): ");
    serial_write_hex(pmm_get_bitmap_bytes());
    (void)serial_write("\r\n");

    (void)serial_write("ItsIB OS: memory map received!\r\n");
    (void)serial_write("Memory map entries: ");
    serial_write_hex(memmap->entry_count);
    (void)serial_write("\r\n");

    uint64_t usable_ram = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_ram += entry->length;
        }
    }

    (void)serial_write("Total usable RAM (bytes): 0x");
    serial_write_hex(usable_ram);
    (void)serial_write("\r\n");


    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        (void)serial_write("Region base: 0x");
        serial_write_hex(entry->base);

        (void)serial_write(" length: 0x");
        serial_write_hex(entry->length);

        (void)serial_write(" type: ");
        serial_write_hex(entry->type);

        (void)serial_write("\r\n");
    }

    if (!serial_write("\r\nItsIB OS: kernel entered successfully!\r\n")) {
        for (;;) __asm__ volatile ("hlt");
    }

    struct limine_framebuffer_response *response =
        framebuffer_request.response;

    if (!response || response->framebuffer_count == 0) {
        (void)serial_write("ERROR: Limine provided no framebuffer.\r\n");
        for (;;) __asm__ volatile ("hlt");
    }

    struct limine_framebuffer *fb = response->framebuffers[0];

    if (!fb || !fb->address || fb->bpp != 32) {
        (void)serial_write("ERROR: unsupported framebuffer format.\r\n");
        for (;;) __asm__ volatile ("hlt");
    }

    fill(fb, 0x101827);

    for (uint64_t y = 0; y < 12; y++)
        for (uint64_t x = 0; x < fb->width; x++)
            pixel(fb, x, y, 0x00B8D4);

    const uint64_t title_width = 8 * 6 * 6;
    const uint64_t welcome_width = 7 * 6 * 3;
    const uint64_t subtitle_width = 26 * 6 * 2;

    const uint64_t block_width = subtitle_width;
    const uint64_t block_height = 114;

    const uint64_t start_x = (fb->width - block_width) / 2;
    const uint64_t start_y = (fb->height - block_height) / 2;

    draw_text(fb, start_x + (block_width - title_width) / 2,
              start_y, "ITSIB OS", 0x00D9FF, 6);

    draw_text(fb, start_x + (block_width - welcome_width) / 2,
              start_y + 52, "WELCOME", 0xFFFFFF, 3);

    draw_text(fb, start_x,
              start_y + 92, "YOUR OWN OPERATING SYSTEM",
              0x8CA3B8, 2);


    if (!serial_write("ItsIB OS: framebuffer initialized.\r\n")) {
        for (;;) __asm__ volatile ("hlt");
    }

    for (;;) __asm__ volatile ("hlt");
}
