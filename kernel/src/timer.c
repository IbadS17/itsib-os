#include "timer.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQUENCY 1193182U
#define TIMER_FREQUENCY 100U

static volatile uint64_t timer_ticks = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}


void timer_irq_handler(void) {
    timer_ticks++;
    outb(PIC1_COMMAND, PIC_EOI);
}

uint64_t timer_get_ticks(void) {
    uint64_t ticks;
    uint64_t flags;

    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    ticks = timer_ticks;

    if (flags & (1ULL << 9)) {
        __asm__ volatile ("sti" ::: "memory");
    }

    return ticks;
}

void timer_init(void) {

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Keep previous masks, then unmask only master IRQ0. */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);

    /* PIT channel 0, square-wave mode, binary counter. */
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / TIMER_FREQUENCY);

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}
