PROJECT := itsib-os
KERNEL := kernel/itsib-kernel.elf
ISO := $(PROJECT).iso
LIMINE := boot/limine
ISO_ROOT := iso

CC := gcc
LD := ld.lld
CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -m64 -mno-red-zone \
          -mcmodel=kernel -mgeneral-regs-only \
          -Wall -Wextra -I $(LIMINE)/limine-protocol/include
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
           -T kernel/linker.ld

.PHONY: all build limine iso run clean check-tools

all: iso

build: $(KERNEL)

$(KERNEL): kernel/src/main.c kernel/linker.ld
	$(CC) -c $< -o kernel/main.o $(CFLAGS)
	$(LD) $(LDFLAGS) -o $@ kernel/main.o

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
	rm -f kernel/main.o $(KERNEL) $(ISO)
	rm -f $(ISO_ROOT)/boot/itsib-kernel.elf
