# mateOS

A minimal educational operating system for learning x86 architecture, written in C and Rust.

32-bit (i686) architecture, bootable via multiboot.

Inspired by experimenting with a simple OS on the 6502.

## Features

### Core System
- **Protected Mode** - Full 32-bit protected mode with GDT
- **Interrupts** - IDT with exception and IRQ handling via PIC
- **Paging** - Identity-mapped 8MB with page-level protection
- **Memory Management** - Dynamic allocation via liballoc

### Multitasking
- **Preemptive Scheduling** - Round-robin scheduler with 100Hz timer
- **Kernel Threads** - Ring 0 tasks with full kernel privileges
- **User Processes** - Ring 3 tasks with memory protection
- **Context Switching** - Full CPU state save/restore with segment handling

### User Mode Support
- **TSS (Task State Segment)** - Kernel stack switching on ring transitions
- **Syscalls** - int 0x80 interface with 3 syscalls:
  - `sys_write(fd, buf, len)` - Console output
  - `sys_exit(code)` - Process termination
  - `sys_yield()` - Voluntary context switch
- **Memory Isolation** - User pages marked non-supervisor, kernel pages protected
- **Separate Stacks** - Each user process has kernel and user stacks

### Other Features
- **Rust Integration** - Hybrid C/Rust kernel with `no_std` Rust code
- **Interactive Console** - Command-line interface with keyboard input
- **VGA Text Mode** - 80x25 character display

## Building

```bash
make              # Build kernel
make test32       # Run in QEMU (i386)
make clean        # Clean build artifacts
```

## Console Commands

- `help` - Show available commands
- `tasks` - List all tasks with Ring level (0=kernel, 3=user)
- `spawn` - Create kernel-mode test tasks
- `usertest` - Create a simple user-mode task
- `demo` - Run mixed Ring 0 + Ring 3 multitasking demo
- `memtest` - Test memory allocator
- `rust` - Test Rust integration
- `uptime` - Show system uptime
- `reboot` - Reboot the system

## Project Structure

### `src/`
Main kernel source files:
- `kernel.c` - Kernel entry point and initialization
- `task.c/h` - Task management and scheduler
- `syscall.c/h` - System call interface
- `console.c/h` - Interactive console and commands

### `src/arch/i686/`
32-bit x86 architecture-specific code:
- `686init.c` - Architecture initialization
- `gdt.c/h` - Global Descriptor Table (6 segments)
- `interrupts.c/h` - IDT and interrupt handlers
- `interrupts_asm.S` - Low-level interrupt stubs
- `tss.c/h` - Task State Segment for ring transitions
- `paging.c/h` - Page table management
- `timer.c/h` - PIT timer driver
- `legacytty.c/h` - VGA text mode driver

### `src/liballoc/`
Memory allocator (liballoc 1.1)

### `rust/`
Rust components compiled with `no_std`

## Architecture Notes

**GDT Layout:**
- 0x00: Null segment
- 0x08: Kernel code (Ring 0)
- 0x10: Kernel data (Ring 0)
- 0x18: User code (Ring 3, RPL=3 → 0x1B)
- 0x20: User data (Ring 3, RPL=3 → 0x23)
- 0x28: TSS

**Memory Map:**
- 0x000000 - 0x100000: Identity mapped (first 1MB)
- 0x100000 - 0x800000: Identity mapped (1-8MB)
- User tasks allocated from heap with user-accessible pages

**Syscall Convention:**
- Vector: int 0x80
- Arguments: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
- Return: eax

## License

Educational/experimental project.
