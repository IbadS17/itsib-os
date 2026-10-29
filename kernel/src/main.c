#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <stdbool.h>
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "kheap.h"
#include "vmm.h"
#include "timer.h"
#include "keyboard.h"
#include "shell.h"

extern void timer_irq_stub(void);
extern void keyboard_irq_stub(void);

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
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
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

static inline uint64_t read_cr3(void) {
    uint64_t value;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(value)
    );

    return value;
}

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


static const uint8_t digit_font[10][7] = {
    {14,17,19,21,25,17,14}, // 0
    {4,12,4,4,4,4,14},      // 1
    {14,17,1,2,4,8,31},     // 2
    {30,1,1,14,1,1,30},     // 3
    {2,6,10,18,31,2,2},     // 4
    {31,16,16,30,1,1,30},   // 5
    {14,16,16,30,17,17,14}, // 6
    {31,1,2,4,8,8,8},       // 7
    {14,17,17,14,17,17,14}, // 8
    {14,17,17,15,1,1,14}    // 9
};

static const uint8_t symbol_font[15][7] = {
    {0,0,0,0,0,4,4},        // .
    {0,0,0,0,0,4,8},        // ,
    {0,4,4,4,0,4,4},        // :
    {0,4,4,4,0,4,0},        // ;
    {0,0,0,31,0,0,0},       // -
    {0,0,14,0,14,0,0},      // =
    {2,4,8,8,8,4,2},        // <
    {8,4,2,2,2,4,8},        // >
    {14,17,2,4,4,0,4},      // ?
    {14,17,1,2,4,0,4},      // !
    {4,14,21,4,4,4,4},      // +
    {0,0,0,31,0,0,0},       // _
    {12,18,20,8,5,19,0},    // %
    {12,18,4,8,18,6,0},     // /
    {12,18,20,8,5,19,0}     // *
};

static void draw_text(struct limine_framebuffer *fb,
                      uint64_t x, uint64_t y,
                      const char *s, uint32_t color, unsigned scale)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        const uint8_t *glyph = NULL;

        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        if (c >= 'A' && c <= 'Z') {
            glyph = font[c - 'A'];
        } else if (c >= '0' && c <= '9') {
            glyph = digit_font[c - '0'];
        } else {
            switch (c) {
                case '.': glyph = symbol_font[0]; break;
                case ',': glyph = symbol_font[1]; break;
                case ':': glyph = symbol_font[2]; break;
                case ';': glyph = symbol_font[3]; break;
                case '-': glyph = symbol_font[4]; break;
                case '=': glyph = symbol_font[5]; break;
                case '<': glyph = symbol_font[6]; break;
                case '>': glyph = symbol_font[7]; break;
                case '?': glyph = symbol_font[8]; break;
                case '!': glyph = symbol_font[9]; break;
                case '+': glyph = symbol_font[10]; break;
                case '_': glyph = symbol_font[11]; break;
                case '%': glyph = symbol_font[12]; break;
                case '/': glyph = symbol_font[13]; break;
                case '*': glyph = symbol_font[14]; break;
                default: break;
            }
        }

        if (glyph) {
            for (unsigned row = 0; row < 7; row++) {
                for (unsigned col = 0; col < 5; col++) {
                    if (glyph[row] & (1u << (4 - col))) {
                        for (unsigned dy = 0; dy < scale; dy++) {
                            for (unsigned dx = 0; dx < scale; dx++) {
                                pixel(fb,
                                      x + col * scale + dx,
                                      y + row * scale + dy,
                                      color);
                            }
                        }
                    }
                }
            }
        }

        x += 6 * scale;
    }
}


#define CONSOLE_BG       0x101827
#define CONSOLE_FG       0xFFFFFF
#define CONSOLE_SCALE    2
#define CONSOLE_CHAR_W   (6 * CONSOLE_SCALE)
#define CONSOLE_CHAR_H   (8 * CONSOLE_SCALE)

static uint64_t console_x;
static uint64_t console_y;
static uint64_t console_start_x;
static uint64_t console_start_y;
static uint64_t console_max_x;
static uint64_t console_max_y;

static void console_draw_char(struct limine_framebuffer *fb, char ch)
{
    char text[2] = { ch, '\0' };
    draw_text(fb, console_x, console_y, text, CONSOLE_FG, CONSOLE_SCALE);
}

static void console_clear_cell(struct limine_framebuffer *fb)
{
    for (uint64_t y = 0; y < CONSOLE_CHAR_H; y++)
        for (uint64_t x = 0; x < CONSOLE_CHAR_W; x++)
            pixel(fb, console_x + x, console_y + y, CONSOLE_BG);
}

static void console_scroll(struct limine_framebuffer *fb)
{
    uint64_t row_height = CONSOLE_CHAR_H;
    uint64_t area_height = console_max_y - console_start_y;

    if (area_height <= row_height)
        return;

    for (uint64_t y = console_start_y;
         y + row_height < console_max_y; y++) {
        for (uint64_t x = console_start_x;
             x < console_max_x; x++) {
            uint32_t *dst = (uint32_t *)(
                (uint8_t *)fb->address +
                y * fb->pitch + x * (fb->bpp / 8));

            uint32_t *src = (uint32_t *)(
                (uint8_t *)fb->address +
                (y + row_height) * fb->pitch +
                x * (fb->bpp / 8));

            *dst = *src;
        }
    }

    for (uint64_t y = console_max_y - row_height;
         y < console_max_y; y++) {
        for (uint64_t x = console_start_x;
             x < console_max_x; x++) {
            pixel(fb, x, y, CONSOLE_BG);
        }
    }
}

static void console_newline(struct limine_framebuffer *fb)
{
    console_x = console_start_x;
    console_y += CONSOLE_CHAR_H;

    if (console_y + CONSOLE_CHAR_H >= console_max_y) {
        console_scroll(fb);
        console_y = console_max_y - CONSOLE_CHAR_H;
    }
}

static void console_putc(struct limine_framebuffer *fb, char ch)
{
    if (ch == '\r')
        return;

    if (ch == '\n') {
        console_newline(fb);
        return;
    }

    if (ch == '\b') {
        if (console_x > console_start_x) {
            console_x -= CONSOLE_CHAR_W;
            console_clear_cell(fb);
        }
        return;
    }

    console_draw_char(fb, ch);
    console_x += CONSOLE_CHAR_W;

    if (console_x + CONSOLE_CHAR_W >= console_max_x)
        console_newline(fb);
}


static struct limine_framebuffer *console_fb;

static void console_clear_input_line(
    uint64_t start_x,
    uint64_t start_y,
    size_t cells
)
{
    if (!console_fb)
        return;

    for (size_t i = 0; i < cells; i++) {
        uint64_t x = start_x + i * CONSOLE_CHAR_W;

        for (uint64_t y = start_y;
             y < start_y + CONSOLE_CHAR_H && y < console_max_y;
             y++) {
            for (uint64_t px = x;
                 px < x + CONSOLE_CHAR_W && px < console_max_x;
                 px++) {
                pixel(console_fb, px, y, CONSOLE_BG);
            }
        }
    }
}

static void shell_write_char(char ch)
{
    console_putc(console_fb, ch);
}

static void shell_clear_console(void)
{
    for (uint64_t y = console_start_y; y < console_max_y; y++) {
        for (uint64_t x = console_start_x; x < console_max_x; x++) {
            pixel(console_fb, x, y, CONSOLE_BG);
        }
    }

    console_x = console_start_x;
    console_y = console_start_y;
}

static void shell_print_prompt(void)
{
    shell_write_char('i');
    shell_write_char('t');
    shell_write_char('s');
    shell_write_char('i');
    shell_write_char('b');
    shell_write_char('>');
    shell_write_char(' ');
}

void itsib_main(void) {
    serial_init();

    struct limine_paging_mode_response *paging_mode =
        paging_mode_request.response;

    if (!paging_mode) {
        kernel_panic("Paging mode response unavailable");
    }

    if (paging_mode->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
        kernel_panic("Expected x86-64 4-level paging");
    }

    (void)serial_write("VMM: 4-level paging confirmed!\r\n");
    uint64_t cr3_value = read_cr3();

    (void)serial_write("Current CR3: 0x");
    serial_write_hex(cr3_value);
    (void)serial_write("\r\n");

    struct limine_hhdm_response *hhdm = hhdm_request.response;

    if (!hhdm) {
        kernel_panic("HHDM response unavailable");
    }

    uint64_t pml4_physical = cr3_value & ~0xFFFULL;
    volatile uint64_t *pml4 =
        (volatile uint64_t *)(hhdm->offset + pml4_physical);

    (void)serial_write("VMM: PML4 physical address: 0x");
    serial_write_hex(pml4_physical);
    (void)serial_write("\r\n");

    (void)serial_write("VMM: PML4 entry 0: 0x");
    serial_write_hex(pml4[0]);
    (void)serial_write("\r\n");

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

    __asm__ volatile ("cli" ::: "memory");

    gdt_init();
    idt_init();

    idt_register_handler(32, timer_irq_stub);
    idt_register_handler(33, keyboard_irq_stub);
    timer_init();
    keyboard_init();

    (void)serial_write("TIMER: PIT initialized at 100 Hz.\r\n");

    struct limine_memmap_response *memmap =
        memmap_request.response;

    if (!memmap) {
        (void)serial_write("ERROR: Limine provided no memory map.\r\n");
        kernel_panic("Memory map unavailable");
    }
    pmm_init(memmap, hhdm->offset);
    if (!vmm_init(hhdm->offset, pml4_physical)) {
        kernel_panic("VMM initialization failed");
    }

    (void)serial_write("VMM: Initialization successful!\r\n");

    uint64_t kernel_page_entry = 0;

    if (!vmm_get_pte(kernel_address->virtual_base,
                     &kernel_page_entry)) {
        kernel_panic("VMM kernel mapping lookup failed");
    }

    (void)serial_write("VMM: Kernel mapping verified! Entry: 0x");
    serial_write_hex(kernel_page_entry);
    (void)serial_write("\r\n");
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
    (void)serial_write("VMM: Running page mapping tests...\r\n");

    const uint64_t vmm_test_address = 0xFFFFC00000000000ULL;
    uint64_t vmm_test_physical = pmm_alloc_page();

    kernel_assert(vmm_test_physical != 0,
                  "VMM test: physical page allocation failed");

    uint64_t existing_translation = 0;

    kernel_assert(!vmm_translate(vmm_test_address,
                                 &existing_translation),
                  "VMM test: test virtual address is already mapped");

    volatile uint64_t *vmm_test_virtual =
        (volatile uint64_t *)vmm_test_address;

    kernel_assert(vmm_map_page(vmm_test_address,
                               vmm_test_physical,
                               VMM_PAGE_WRITABLE),
                  "VMM test: mapping creation failed");

    uint64_t translated_physical = 0;

    kernel_assert(vmm_translate(vmm_test_address,
                                &translated_physical),
                  "VMM test: address translation failed");

    kernel_assert(translated_physical == vmm_test_physical,
                  "VMM test: physical translation mismatch");

    *vmm_test_virtual = 0x4954534942564D4DULL;

    kernel_assert(*vmm_test_virtual == 0x4954534942564D4DULL,
                  "VMM test: mapped memory read/write failed");

    uint64_t unmapped_physical = 0;

    kernel_assert(vmm_unmap_page(vmm_test_address,
                                 &unmapped_physical),
                  "VMM test: unmapping failed");

    kernel_assert(unmapped_physical == vmm_test_physical,
                  "VMM test: unmap returned incorrect physical address");

    kernel_assert(!vmm_translate(vmm_test_address,
                                 &existing_translation),
                  "VMM test: translation succeeded after unmapping");

    kernel_assert(pmm_free_page(vmm_test_physical),
                  "VMM test: physical page cleanup failed");

    (void)serial_write("VMM: Page mapping tests passed!\r\n");
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
        (void)serial_write("KHEAP: Testing block coalescing...\r\n");

    void *coal_a = kmalloc(256);
    void *coal_b = kmalloc(256);
    void *coal_c = kmalloc(256);

    kernel_assert(coal_a != NULL && coal_b != NULL && coal_c != NULL,
                  "KHEAP test: coalescing allocations failed");

    kernel_assert((uintptr_t)coal_b > (uintptr_t)coal_a &&
                  (uintptr_t)coal_c > (uintptr_t)coal_b,
                  "KHEAP test: allocations are not in expected order");

    kernel_assert(kfree(coal_a),
                  "KHEAP test: freeing coalescing block A failed");

    kernel_assert(kfree(coal_b),
                  "KHEAP test: freeing coalescing block B failed");

    void *coal_large = kmalloc(400);

    kernel_assert(coal_large == coal_a,
                  "KHEAP test: adjacent free blocks did not coalesce");

    kernel_assert(kfree(coal_large),
                  "KHEAP test: freeing merged allocation failed");

    kernel_assert(kfree(coal_c),
                  "KHEAP test: freeing coalescing block C failed");

    (void)serial_write("KHEAP: Block coalescing test passed!\r\n");
    (void)serial_write("KHEAP: Testing freed memory reuse...\r\n");

    void *reuse_a = kmalloc(64);

    kernel_assert(reuse_a != NULL,
                  "KHEAP test: reuse allocation A failed");

    kernel_assert(kfree(reuse_a),
                  "KHEAP test: freeing reuse allocation A failed");

    void *reuse_b = kmalloc(64);

    kernel_assert(reuse_b == reuse_a,
                  "KHEAP test: freed memory was not reused");

    kernel_assert(kfree(reuse_b),
                  "KHEAP test: freeing reused allocation failed");

    (void)serial_write("KHEAP: Freed memory reuse test passed!\r\n");

    (void)serial_write("KHEAP: Testing fragmentation...\r\n");

    void *frag_a = kmalloc(256);
    void *frag_b = kmalloc(256);
    void *frag_c = kmalloc(256);

    kernel_assert(frag_a != NULL && frag_b != NULL && frag_c != NULL,
                  "KHEAP test: fragmentation allocations failed");

    kernel_assert(kfree(frag_a),
                  "KHEAP test: freeing fragment A failed");

    kernel_assert(kfree(frag_c),
                  "KHEAP test: freeing fragment C failed");

    void *frag_large = kmalloc(512);

    kernel_assert(frag_large != NULL,
                  "KHEAP test: allocation after fragmentation failed");

    kernel_assert(frag_large != frag_a,
                  "KHEAP test: separate free regions were merged incorrectly");

    kernel_assert(kfree(frag_large),
                  "KHEAP test: freeing fragmented allocation failed");

    kernel_assert(kfree(frag_b),
                  "KHEAP test: freeing fragment B failed");

    (void)serial_write("KHEAP: Fragmentation test passed!\r\n");
    (void)serial_write("KHEAP: Testing heap growth...\r\n");

    void *growth_a = kmalloc(3000);
    void *growth_b = kmalloc(3000);

    kernel_assert(growth_a != NULL,
                  "KHEAP test: first growth allocation failed");

    kernel_assert(growth_b != NULL,
                  "KHEAP test: second growth allocation failed");

    kernel_assert(growth_a != growth_b,
                  "KHEAP test: growth allocations overlap");

    kernel_assert(((uintptr_t)growth_a % 16) == 0 &&
                  ((uintptr_t)growth_b % 16) == 0,
                  "KHEAP test: growth allocation is not aligned");

    kernel_assert(kfree(growth_a),
                  "KHEAP test: freeing growth allocation A failed");

    kernel_assert(kfree(growth_b),
                  "KHEAP test: freeing growth allocation B failed");

    (void)serial_write("KHEAP: Heap growth test passed!\r\n");

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

        /* Enable hardware interrupts and verify PIT timer delivery. */
    __asm__ volatile ("sti" ::: "memory");

    uint64_t start_ticks = timer_get_ticks();
    uint64_t target_ticks = start_ticks + 5;

    while (timer_get_ticks() < target_ticks) {
        __asm__ volatile ("hlt" ::: "memory");
    }

    (void)serial_write("TIMER: 5 ticks received successfully!\r\n");

    /* Print received keyboard characters to the serial console. */
    (void)serial_write("KEYBOARD: Press keys in the QEMU window.\r\n");

    console_start_x = 24;
    console_start_y = fb->height / 2 + 80;
    console_x = console_start_x;
    console_y = console_start_y;
    console_max_x = fb->width - 24;
    console_max_y = fb->height - 16;

    (void)serial_write("KEYBOARD: Framebuffer console ready.\r\n");

    console_fb = fb;

    #define COMMAND_SIZE 128
    #define HISTORY_SIZE 8

    char command[COMMAND_SIZE] = {0};
    char history[HISTORY_SIZE][COMMAND_SIZE] = {{0}};
    size_t command_length = 0;
    size_t history_count = 0;
    size_t history_next = 0;
    size_t history_position = 0;
    uint64_t input_start_x = 0;
    uint64_t input_start_y = 0;

    shell_print_prompt();
    input_start_x = console_x;
    input_start_y = console_y;

    for (;;) {
        keyboard_event_t event;

        if (keyboard_get_event(&event)) {
            if (event.type == KEYBOARD_EVENT_UP ||
                event.type == KEYBOARD_EVENT_DOWN) {
                if (history_count > 0) {
                    if (event.type == KEYBOARD_EVENT_UP) {
                        if (history_position > 0)
                            history_position--;
                    } else if (history_position < history_count) {
                        history_position++;
                    }

                    size_t old_length = command_length;

                    /* Erase the old input pixels, preserving the prompt. */
                    console_clear_input_line(
                        input_start_x, input_start_y, old_length
                    );

                    console_x = input_start_x;
                    console_y = input_start_y;

                    if (history_position == history_count) {
                        command_length = 0;
                        command[0] = '\0';
                    } else {
                        size_t oldest =
                            (history_next + HISTORY_SIZE - history_count)
                            % HISTORY_SIZE;
                        size_t slot =
                            (oldest + history_position) % HISTORY_SIZE;
                        size_t i = 0;

                        while (i < COMMAND_SIZE - 1 &&
                               history[slot][i] != '\0') {
                            command[i] = history[slot][i];
                            i++;
                        }

                        command[i] = '\0';
                        command_length = i;
                    }

                    for (size_t i = 0; i < command_length; i++)
                        console_putc(console_fb, command[i]);
                }
            } else if (event.type == KEYBOARD_EVENT_CHAR) {
                char key = event.character;

                if (key == '\r' || key == '\n') {
                    shell_write_char('\n');
                    (void)serial_write("\r\n");

                    command[command_length] = '\0';

                    if (command_length > 0) {
                        size_t slot = history_next;

                        size_t i = 0;
                        while (i < COMMAND_SIZE - 1 &&
                               command[i] != '\0') {
                            history[slot][i] = command[i];
                            i++;
                        }
                        history[slot][i] = '\0';

                        history_next = (history_next + 1) % HISTORY_SIZE;
                        if (history_count < HISTORY_SIZE)
                            history_count++;
                    }

                    shell_execute(command, shell_write_char,
                                  shell_clear_console);

                    command_length = 0;
                    command[0] = '\0';
                    shell_print_prompt();
                    input_start_x = console_x;
                    input_start_y = console_y;
                    history_position = history_count;
                } else if (key == '\b') {
                    if (command_length > 0) {
                        command_length--;
                        command[command_length] = '\0';
                        shell_write_char('\b');
                        (void)serial_write("\b \b");
                    }
                } else if (key >= 32 && key <= 126 &&
                           command_length < COMMAND_SIZE - 1) {
                    command[command_length++] = key;
                    command[command_length] = '\0';
                    shell_write_char(key);

                    char output[2] = { key, '\0' };
                    (void)serial_write(output);
                }
            }
        } else {
            __asm__ volatile ("sti; hlt" ::: "memory");
        }
    }
}
