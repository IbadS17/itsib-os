# ItsIB OS — Architecture

## Project Vision
ItsIB OS is an independent operating system designed for
x86-64 computers, including the HP Victus 15.

## Development Environment
- Host: Fedora Linux
- Architecture: x86-64
- Kernel language: C
- Low-level language: Assembly
- Bootloader: Limine
- Emulator: QEMU
- Version control: Git

## Development Milestones

### Phase 1: Bootable Kernel
- Boot our own kernel.
- Display "Welcome to ItsIB OS".
- Handle CPU exceptions and interrupts.

### Phase 2: Core Kernel
- Physical memory management.
- Virtual memory management.
- Kernel heap.
- Timer and keyboard input.

### Phase 3: Operating System Services
- Process and thread scheduling.
- System calls.
- Filesystem.
- Storage and networking drivers.

### Phase 4: ItsIB Desktop
- Graphics subsystem.
- Window manager and compositor.
- Application launcher.
- ItsIB Files and ItsIB Settings.

### Phase 5: Distribution
- Bootable ISO.
- Graphical installer.
- Hardware testing on the HP Victus 15.

## Design Principles
- Independent kernel; no Linux kernel dependency.
- Test in QEMU before physical hardware.
- Keep changes under version control.
- Implement and test one subsystem at a time.
