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
- **Background Jobs** - Shell supports `&` suffix to run tasks in background with `jobs` tracking

### Process Management
- **spawn + wait** - Fork-like process creation from ELF binaries
- **Non-blocking wait** - `wait_nb()` polls child status without blocking
- **Process Isolation** - Child processes run in separate address spaces, parent memory is never touched
- **ELF Loader** - Loads ELF32 binaries from ramfs into per-process physical frames
- **Exit Codes** - Processes return exit codes to their parent via wait()

### Networking
- **RTL8139 NIC Driver** - PCI-based Ethernet driver in `src/drivers/`
- **lwIP TCP/IP Stack** - Full IPv4 networking (ARP, ICMP, TCP, UDP)
- **ICMP Ping** - `ping` shell command and `net_ping()` syscall
- **TCP Sockets** - Kernel socket table with listen/accept/send/recv/close syscalls
- **HTTP Server** - Userland `httpd.elf` serves HTML on port 80
- **Network Configuration** - `ifconfig` command to set/view IP, netmask, gateway
- **DHCP via QEMU** - Automatic IP configuration with QEMU user-mode networking

### Window Manager
- **Compositing WM** - `gui.elf` owns the framebuffer and composites child windows
- **Window Syscalls** - create, destroy, read, write, getkey, sendkey, list
- **2x2 Tiled Layout** - Up to 4 windows in a grid with title bars and borders
- **Focus Management** - Tab key cycles focus between windows
- **Window Terminal** - `winterm.elf` provides a shell inside a WM window
- **Double-Buffered Windows** - Child apps render to pixel buffers, WM composites flicker-free

### User Mode Support
- **TSS (Task State Segment)** - Kernel stack switching on ring transitions
- **Syscalls** - int 0x80 interface with 32 syscalls:
  - `write(fd, buf, len)` - Console output
  - `exit(code)` - Process termination with exit code
  - `yield()` - Voluntary context switch
  - `exec(filename)` - Replace current process with ELF from ramfs
  - `gfx_init()` - Enter graphics mode (BGA 1024x768 or VGA 320x200)
  - `gfx_exit()` - Return to text mode
  - `getkey(flags)` - Non-blocking keyboard input
  - `spawn(filename)` - Create child process from ELF
  - `wait(pid)` - Block until child exits, get exit code
  - `readdir(index, buf, size)` - List ramfs directory entries
  - `getpid()` - Get current process ID
  - `taskinfo()` - Print task list to console
  - `shutdown()` - ACPI power off
  - `win_create/destroy/write/read/getkey/sendkey/list` - Window management
  - `gfx_info()` - Get screen dimensions
  - `tasklist()` - Get task list as structured data
  - `wait_nb(pid)` - Non-blocking wait
  - `net_ping()` - ICMP ping
  - `net_cfg/net_get()` - Network configuration
  - `sleep_ms()` - Millisecond sleep
  - `sock_listen/accept/send/recv/close()` - TCP sockets
- **Memory Isolation** - User pages marked non-supervisor, kernel pages protected
- **Separate Stacks** - Each user process has independent kernel and user stacks

### Graphics
- **Bochs VGA (BGA)** - 1024x768 with 256-color palette via Bochs dispi registers
- **VGA Mode 13h Fallback** - 320x200 with 256 colors when BGA unavailable
- **VGA Text Mode** - 80x25 character display with scrolling
- **Userland Graphics Library** - `ugfx.h` provides pixel drawing, rectangles, text rendering, buffer operations

### Filesystem
- **Ramfs** - In-memory filesystem loaded from multiboot initrd module
- **Initrd Tool** - `tools/mkinitrd` packs ELF binaries into a bootable initrd image

### Other
- **Rust Integration** - Hybrid C/Rust kernel with `no_std` Rust components
- **Keyboard Driver** - PS/2 keyboard with scancode translation, shift key support, buffered input
- **Userland Shell** - Interactive command-line shell running in Ring 3
- **Test Suite** - 15-test userland test suite covering syscalls, memory, process isolation

## Building

```bash
make userland                # Build all userland programs
make initrd                  # Build userland + pack into initrd.img
make                         # Build kernel (dmos.bin)
make run                     # Run in QEMU (text mode)
```

```bash
make clean                   # Clean everything (kernel + userland + initrd)
```

### Running with QEMU

The `make run` target is composable with flags:

```bash
make run                          # Text mode, no networking
make run GFX=1                    # SDL graphics (BGA 1024x768)
make run VNC=1                    # VNC display (connect to port 5900)
make run NET=1                    # User-mode networking
make run NET=tap                  # TAP networking
make run NET=1 HTTP=1             # Networking + port forward 8080->80
make run GFX=1 NET=1 HTTP=1      # Graphics + networking + HTTP
make run VNC=1 NET=1 HTTP=1      # VNC + networking + HTTP
```

Flags can be combined freely:
- **GFX=1** - SDL display with `-vga std` (required for BGA graphics)
- **VNC=1** - VNC display on `:0` (port 5900) with `-vga std`
- **NET=1** - QEMU user-mode networking with RTL8139
- **NET=tap** - TAP networking with RTL8139
- **HTTP=1** - Port forward host 8080 to guest 80 (use with NET=1)

### Testing the HTTP Server

```bash
# Terminal 1: start OS with networking
make run NET=1 HTTP=1

# In the mateOS shell:
$ httpd.elf &

# Terminal 2: test from host
curl http://localhost:8080
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
- `tasks` - Show all tasks with PID, state, and name
- `echo <text>` - Print text
- `ping <ip>` - Ping an IP address (e.g. `ping 10.0.2.2`)
- `ifconfig [ip mask gw]` - Show or set network configuration
- `jobs` - List background jobs
- `clear` - Clear screen
- `shutdown` - Power off (ACPI)
- `exit` - Exit shell

Run any program by name: `hello.elf`, `test.elf`, `gui.elf`

Append `&` to run in background: `httpd.elf &`

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
- `syscall.c/h` - System call dispatcher and handlers (32 syscalls)
- `pmm.c/h` - Physical memory manager (bitmap frame allocator)
- `elf.c/h` - ELF32 binary loader
- `ramfs.c/h` - In-memory filesystem
- `multiboot.c/h` - Multiboot info parsing, initrd detection
- `console.c/h` - Early boot console
- `keyboard.c/h` - PS/2 keyboard driver with shift support and input buffering
- `net.c/h` - Networking layer (lwIP integration, TCP socket table, ICMP ping)
- `pci.c/h` - PCI bus enumeration

### `src/drivers/`
Hardware drivers:
- `rtl8139.c/h` - RTL8139 NIC driver (PCI, DMA, interrupt-driven)

### `src/arch/i686/`
x86 architecture-specific code:
- `686init.c/h` - Architecture initialization, page table storage
- `gdt.c/h` - Global Descriptor Table (6 segments)
- `interrupts.c/h` - IDT setup, exception/IRQ handlers
- `interrupts_asm.S` - Low-level interrupt stubs, syscall entry, context switch
- `tss.c/h` - Task State Segment for ring transitions
- `paging.c/h` - Page directory/table management, per-process address spaces
- `timer.c/h` - PIT timer driver (100Hz)
- `vga.c/h` - BGA/VGA graphics driver
- `legacytty.c/h` - VGA text mode driver
- `boot.S` - Multiboot header and kernel entry

### `src/liballoc/`
Memory allocator (liballoc 1.1) with bump-allocator hooks

### `src/lwip/`
lwIP TCP/IP stack (NO_SYS=1 mode) providing ARP, IPv4, ICMP, TCP, UDP

### `rust/`
Rust components compiled with `no_std` and custom i686 target

### `userland/`
User-space programs:
- `shell.c` - Interactive shell with background job support
- `hello.c` - Hello world test program
- `test.c` - Comprehensive test suite (15 tests)
- `gui.c` - Window manager (compositing WM, 2x2 tiled layout)
- `winterm.c` - Terminal emulator running inside WM windows
- `winhello.c` - Window hello world demo
- `winedit.c` - Window text editor demo
- `winsleep.c` - Window sleep demo
- `httpd.c` - HTTP server (serves HTML on port 80)
- `ping.c` - Standalone ping utility
- `ugfx.c/h` - Userland graphics library (pixel, rect, text, buffer ops)
- `syscalls.c/h` - Syscall wrappers (int 0x80, 32 syscalls)
- `cmd_shared.c/h` - Shared shell builtins (used by shell and winterm)
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
| 0xFD000000+ | BGA linear framebuffer (PCI BAR0, mapped at runtime) |

**Paging:**
- 8 page tables identity-map 0-32MB for kernel access
- Per-process page directories share kernel page tables (0-7)
- Page table 1 (0x400000-0x7FFFFF) is copied per-process: heap entries shared, user code entries (0x700000+) are private
- ELF segments loaded into PMM frames, mapped at virtual addresses in process page directory
- BGA framebuffer pages identity-mapped into graphics-owning process
- CR3 swapped on every context switch

**Syscall Convention:**
- Vector: int 0x80
- Arguments: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
- Return: eax
- iret frame pointer passed as 5th argument for exec()

## License

Educational/experimental project.
