
bits 64

section .text
global idt_load

idt_load:
    lidt [rdi]
    ret

global idt_default_handler

idt_default_handler:
    cli

.halt:
    hlt
    jmp .halt
