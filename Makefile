PROJECT := itsib-os
KERNEL := kernel/itsib-kernel.elf
ISO := $(PROJECT).iso
LIMINE := boot/limine
ISO_ROOT := iso

CC := gcc
LD := ld.lld
NASM := nasm
CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -m64 -mno-red-zone \
          -mcmodel=kernel -mgeneral-regs-only \
          -Wall -Wextra -I $(LIMINE)/limine-protocol/include
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
           -T kernel/linker.ld

.PHONY: all build limine iso run clean check-tools

all: iso

build: $(KERNEL)

$(KERNEL): kernel/main.o kernel/gdt.o kernel/gdt_flush.o kernel/idt.o kernel/idt_load.o kernel/pmm.o kernel/kheap.o kernel/vmm.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ kernel/main.o kernel/gdt.o kernel/gdt_flush.o kernel/idt.o kernel/idt_load.o kernel/pmm.o kernel/kheap.o kernel/vmm.o

kernel/main.o: kernel/src/main.c
	$(CC) -c $< -o $@ $(CFLAGS)

kernel/gdt.o: kernel/src/gdt.c kernel/src/gdt.h
	$(CC) -c $< -o $@ $(CFLAGS)

kernel/gdt_flush.o: kernel/src/gdt_flush.asm
	$(NASM) -f elf64 $< -o $@

kernel/idt.o: kernel/src/idt.c kernel/src/idt.h
	$(CC) -c $< -o $@ $(CFLAGS)

kernel/idt_load.o: kernel/src/idt_load.asm
	$(NASM) -f elf64 $< -o $@

kernel/pmm.o: kernel/src/pmm.c kernel/src/pmm.h
	$(CC) -c $< -o $@ $(CFLAGS)

kernel/kheap.o: kernel/src/kheap.c kernel/src/kheap.h kernel/src/pmm.h
	$(CC) -c $< -o $@ $(CFLAGS)

kernel/vmm.o: kernel/src/vmm.c kernel/src/vmm.h kernel/src/pmm.h
	$(CC) -c $< -o $@ $(CFLAGS)

limine:
	cd $(LIMINE) && ./configure --enable-uefi-x86-64 --enable-uefi-cd
	$(MAKE) -C $(LIMINE) -j$$(nproc)

iso: $(KERNEL) limine
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/itsib-kernel.elf
	cp $(LIMINE)/bin/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp $(LIMINE)/bin/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/limine-uefi-cd.bin
	xorriso -as mkisofs -R -r -J \
	  --efi-boot boot/limine/limine-uefi-cd.bin \
	  -efi-boot-part --efi-boot-image --protective-msdos-label \
	  $(ISO_ROOT) -o $(ISO)

run: iso
	qemu-system-x86_64 -machine q35 -m 2G \
	  -bios /usr/share/OVMF/OVMF_CODE.fd \
	  -cdrom $(ISO) -boot d -serial stdio

check-tools:
	@command -v gcc
	@command -v ld.lld
	@command -v xorriso
	@command -v qemu-system-x86_64

clean:
	rm -f kernel/main.o kernel/gdt.o kernel/gdt_flush.o kernel/idt.o kernel/idt_load.o kernel/pmm.o kernel/kheap.o kernel/vmm.o $(KERNEL) $(ISO)
	rm -f $(ISO_ROOT)/boot/itsib-kernel.elf
