# mateOS

A minimal educational operating system for learning x86 architecture, written in C and Rust.

32-bit (i686) architecture, bootable via multiboot.

Inspired by experimenting with a simple OS on the 6502.

## Features

### Core System
- **Protected Mode** - Full 32-bit protected mode with GDT (6 segments)
- **Interrupts** - IDT with 256 entries, exception and IRQ handling via PIC
- **Paging** - Identity-mapped 32MB with per-process page directories
- **Physical Memory Manager** - Bitmap-based allocator for 4KB frames (8-32MB range, 6144 frames)
- **Heap Allocator** - Dynamic kernel allocation via liballoc (0x400000-0x600000)

### Multitasking
- **Preemptive Scheduling** - Round-robin scheduler with 100Hz PIT timer
- **Kernel Threads** - Ring 0 tasks with full kernel privileges
- **User Processes** - Ring 3 tasks with hardware memory protection
- **Context Switching** - Full CPU state save/restore including segment registers, CR3 swap
- **Per-Process Address Spaces** - Each user process gets its own page directory, isolating virtual memory at 0x700000

### Process Management
- **spawn + wait** - Fork-like process creation from ELF binaries
- **Process Isolation** - Child processes run in separate address spaces, parent memory is never touched
- **ELF Loader** - Loads ELF32 binaries from ramfs into per-process physical frames
- **Exit Codes** - Processes return exit codes to their parent via wait()

### User Mode Support
- **TSS (Task State Segment)** - Kernel stack switching on ring transitions
- **Syscalls** - int 0x80 interface with 13 syscalls:
  - `write(fd, buf, len)` - Console output
  - `exit(code)` - Process termination with exit code
  - `yield()` - Voluntary context switch
  - `exec(filename)` - Replace current process with ELF from ramfs
  - `gfx_init()` - Enter VGA Mode 13h (320x200, 256 colors)
  - `gfx_exit()` - Return to text mode
  - `getkey(flags)` - Non-blocking keyboard input
  - `spawn(filename)` - Create child process from ELF
  - `wait(pid)` - Block until child exits, get exit code
  - `readdir(index, buf, size)` - List ramfs directory entries
  - `getpid()` - Get current process ID
  - `taskinfo()` - Print task list to console
  - `shutdown()` - ACPI power off
- **Memory Isolation** - User pages marked non-supervisor, kernel pages protected
- **Separate Stacks** - Each user process has independent kernel and user stacks

### Filesystem
- **Ramfs** - In-memory filesystem loaded from multiboot initrd module
- **Initrd Tool** - `tools/mkinitrd` packs ELF binaries into a bootable initrd image

### Graphics
- **VGA Mode 13h** - 320x200 with 256-color palette, accessible from userland
- **VGA Text Mode** - 80x25 character display with scrolling
- **Userland Graphics Library** - `ugfx.h` provides pixel drawing, rectangles, text rendering

### Other
- **Rust Integration** - Hybrid C/Rust kernel with `no_std` Rust components
- **Keyboard Driver** - PS/2 keyboard with scancode translation, buffered input
- **Userland Shell** - Interactive command-line shell running in Ring 3
- **Test Suite** - 15-test userland test suite covering syscalls, memory, process isolation

## Building

```bash
# Build everything and run
cd userland && make            # Build user programs
cd ..
./tools/mkinitrd initrd.img userland/shell.elf userland/hello.elf userland/test.elf userland/gui.elf
make                           # Build kernel
make test32                    # Run in QEMU (text mode)
make test32-gfx                # Run in QEMU (graphical, for gui.elf)
```

```bash
make clean                     # Clean build artifacts
```

### Requirements
- `clang` (cross-compilation target: i686-unknown-none-elf)
- `gcc` (for userland, 32-bit support: `gcc -m32`)
- `qemu-system-i386`
- Rust nightly toolchain (for Rust components)

## Shell Commands

The shell runs as a Ring 3 user process (`shell.elf`):

- `help` - Show available commands
- `ls` - List files in ramfs
- `tasks` - Show all tasks with PID, state, and ring level
- `echo <text>` - Print text
- `clear` - Clear screen
- `shutdown` - Power off (ACPI)
- `exit` - Exit shell

Run any program by name: `hello.elf`, `test.elf`, `gui.elf`

## Test Suite

`test.elf` is a comprehensive userland test suite. Run it from the shell:

```
$ test.elf
```

Tests cover:
1. Basic syscalls (write, yield)
2. String operations
3. Math (addition, multiplication, division, modulo)
4. Stack operations (local arrays)
5. Function calls (factorial, fibonacci recursion)
6. Global and BSS data sections
7. Cooperative scheduling (multiple yields)
8. Memory patterns (ascending, alternating, fill/zero)
9. getpid syscall (stability across calls)
10. readdir syscall (ramfs directory listing)
11. spawn + wait (child process lifecycle)
12. Spawn error handling (non-existent files)
13. Write return value validation
14. Deep stack usage (recursion with padding, large locals)
15. Process isolation (memory markers survive child spawn)

## Project Structure

### `src/`
Main kernel source files:
- `kernel.c` - Kernel entry point and initialization
- `task.c/h` - Task management, scheduler, per-process CR3 switching
- `syscall.c/h` - System call dispatcher and handlers
- `pmm.c/h` - Physical memory manager (bitmap frame allocator)
- `elf.c/h` - ELF32 binary loader
- `ramfs.c/h` - In-memory filesystem
- `multiboot.c/h` - Multiboot info parsing, initrd detection
- `console.c/h` - Early boot console
- `keyboard.c/h` - PS/2 keyboard driver with input buffering

### `src/arch/i686/`
x86 architecture-specific code:
- `686init.c/h` - Architecture initialization, page table storage
- `gdt.c/h` - Global Descriptor Table (6 segments)
- `interrupts.c/h` - IDT setup, exception/IRQ handlers
- `interrupts_asm.S` - Low-level interrupt stubs, syscall entry, context switch
- `tss.c/h` - Task State Segment for ring transitions
- `paging.c/h` - Page directory/table management, per-process address spaces
- `timer.c/h` - PIT timer driver (100Hz)
- `vga.c/h` - VGA Mode 13h driver
- `legacytty.c/h` - VGA text mode driver
- `boot.S` - Multiboot header and kernel entry

### `src/liballoc/`
Memory allocator (liballoc 1.1) with bump-allocator hooks

### `rust/`
Rust components compiled with `no_std` and custom i686 target

### `userland/`
User-space programs:
- `shell.c` - Interactive shell
- `hello.c` - Hello world test program
- `test.c` - Comprehensive test suite (15 tests)
- `gui.c` - Graphics demo (VGA Mode 13h)
- `ugfx.c/h` - Userland graphics library
- `syscalls.c/h` - Syscall wrappers (int 0x80)
- `user.ld` - Linker script (loads at 0x700000)

### `tools/`
- `mkinitrd` - Initrd image builder

## Architecture Notes

**GDT Layout:**
| Selector | Segment | Ring |
|----------|---------|------|
| 0x00 | Null | - |
| 0x08 | Kernel code | 0 |
| 0x10 | Kernel data | 0 |
| 0x18 (0x1B) | User code | 3 |
| 0x20 (0x23) | User data | 3 |
| 0x28 | TSS | - |

**Memory Map:**
| Range | Usage |
|-------|-------|
| 0x000000 - 0x0FFFFF | Low memory (BIOS, VGA at 0xA0000) |
| 0x100000 - 0x1FFFFF | Kernel code and data |
| 0x200000 - 0x26FFFF | Kernel BSS, GDT, IDT, page tables, TSS |
| 0x400000 - 0x5FFFFF | Kernel heap (liballoc) |
| 0x700000 - 0x7FFFFF | User code region (per-process, virtual) |
| 0x7F0000 - 0x7F0FFF | User stack (per-process, virtual) |
| 0x800000 - 0x1FFFFFF | PMM-managed physical frames (24MB) |
| 0xA0000 - 0xAFFFF | VGA framebuffer (Mode 13h) |

**Paging:**
- 8 page tables identity-map 0-32MB for kernel access
- Per-process page directories share kernel page tables (0-7)
- Page table 1 (0x400000-0x7FFFFF) is copied per-process: heap entries shared, user code entries (0x700000+) are private
- ELF segments loaded into PMM frames, mapped at virtual addresses in process page directory
- CR3 swapped on every context switch

**Syscall Convention:**
- Vector: int 0x80
- Arguments: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
- Return: eax
- iret frame pointer passed as 5th argument for exec()

## License

Educational/experimental project.
