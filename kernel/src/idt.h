#ifndef ITSIB_IDT_H
#define ITSIB_IDT_H

#include <stdint.h>

void idt_init(void);
void idt_register_handler(uint8_t vector, void (*handler)(void));

#endif
