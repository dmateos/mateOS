# mateOS

A minimal educational operating system for learning x86 architecture, written in C and Rust.

32-bit (i686) architecture, bootable via multiboot.

Inspired by experimenting with a simple OS on the 6502.

![Window manager with terminal, file manager, and task manager](s1.png)

![Running on Proxmox with HTTP server](s2.png)

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
- **argc/argv** - Programs receive command-line arguments via `_start(int argc, char **argv)`
- **Non-blocking wait** - `wait_nb()` polls child status without blocking
- **Process Isolation** - Child processes run in separate address spaces, parent memory is never touched
- **ELF Loader** - Loads ELF32 binaries from ramfs into per-process physical frames
- **Exit Codes** - Processes return exit codes to their parent via wait()

### Networking
- **RTL8139 NIC Driver** - PCI-based Ethernet driver in `src/drivers/`
- **lwIP TCP/IP Stack** - Full IPv4 networking (ARP, ICMP, TCP, UDP)
- **ICMP Ping** - `ping` command and `net_ping()` syscall
- **TCP Sockets** - Kernel socket table with listen/accept/send/recv/close syscalls
- **HTTP Server** - Userland `httpd` serves HTML on port 80
- **Network Configuration** - `ifconfig` command to set/view IP, netmask, gateway
- **DHCP via QEMU** - Automatic IP configuration with QEMU user-mode networking

### Window Manager
- **Compositing WM** - `gui` owns the framebuffer and composites child windows
- **Window Syscalls** - create, destroy, read, write, getkey, sendkey, list
- **2x2 Tiled Layout** - Up to 4 windows in a grid with title bars and borders
- **Focus Management** - Tab key cycles focus between windows
- **Window Terminal** - `winterm` provides a shell inside a WM window
- **Double-Buffered Windows** - Child apps render to pixel buffers, WM composites flicker-free
- **Mouse Support** - PS/2 mouse driver with cursor tracking

### Filesystem
- **Virtual File System (VFS)** - Abstraction layer supporting multiple filesystem backends
- **Ramfs** - In-memory filesystem loaded from multiboot initrd module
- **Per-Process File Descriptors** - Each task has its own FD table (16 max)
- **File I/O Syscalls** - open, read, write, close, seek, stat
- **Initrd Tool** - `tools/mkinitrd` packs ELF binaries into a bootable initrd image

### User Mode Support
- **TSS (Task State Segment)** - Kernel stack switching on ring transitions
- **41 Syscalls** via int 0x80:
  - **Process:** write, exit, yield, exec, spawn, wait, wait_nb, getpid, tasklist, shutdown, sleep_ms
  - **Graphics:** gfx_init, gfx_exit, gfx_info, getmouse
  - **Keyboard:** getkey
  - **Filesystem:** readdir, open, fread, fwrite, close, seek, stat
  - **Window Manager:** win_create, win_destroy, win_write, win_read, win_getkey, win_sendkey, win_list, win_read_text, win_set_stdout
  - **Networking:** net_ping, net_cfg, net_get, sock_listen, sock_accept, sock_send, sock_recv, sock_close
- **Memory Isolation** - User pages marked non-supervisor, kernel pages protected
- **Separate Stacks** - Each user process has independent kernel and user stacks

### Graphics
- **Bochs VGA (BGA)** - 1024x768 with 256-color palette via Bochs dispi registers
- **VGA Mode 13h Fallback** - 320x200 with 256 colors when BGA unavailable
- **VGA Text Mode** - 80x25 character display with scrolling
- **Userland Graphics Library** - `ugfx.h` provides pixel drawing, rectangles, text rendering, buffer operations

### Other
- **Rust Integration** - Hybrid C/Rust kernel with `no_std` Rust components
- **Keyboard Driver** - PS/2 keyboard with scancode translation, shift key support, buffered input
- **Mouse Driver** - PS/2 mouse with position tracking and button state
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
$ httpd &

# Terminal 2: test from host
curl http://localhost:8080
```

### Requirements
- `clang` (cross-compilation target: i686-unknown-none-elf)
- `gcc` (for userland, 32-bit support: `gcc -m32`)
- `qemu-system-i386`
- Rust nightly toolchain (for Rust components)

## Shell

The shell runs as a Ring 3 user process (`shell.elf`). Programs can be run by name without the `.elf` extension — the shell appends it automatically.

### Built-in Commands

- `help` - Show available commands
- `clear` - Clear screen
- `exit` - Exit shell
- `jobs` - List background jobs

### Standalone Programs

These are separate ELF binaries invoked by name:

- `ls` - List files in ramfs
- `tasks` - Show all tasks with PID, state, and name
- `echo <text>` - Print text
- `cat <file>` - Display file contents
- `ping <ip>` - Ping an IP address (e.g. `ping 10.0.2.2`)
- `ifconfig [ip mask gw]` - Show or set network configuration
- `shutdown` - Power off (ACPI)
- `hello` - Hello world demo
- `test` - Run comprehensive test suite
- `gui` - Start window manager
- `winterm` - Terminal emulator (inside WM)
- `httpd` - HTTP server (port 80)

Run any program by name: `hello`, `test`, `gui`

Append `&` to run in background: `httpd &`

## Test Suite

`test` is a comprehensive userland test suite. Run it from the shell:

```
$ test
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

## Syscall Reference

41 syscalls via int 0x80 (eax=syscall#, ebx/ecx/edx=args, return in eax):

| # | Name | Signature | Description |
|---|------|-----------|-------------|
| 1 | SYS_WRITE | write(fd, buf, len) | Console/window output |
| 2 | SYS_EXIT | exit(code) | Terminate process |
| 3 | SYS_YIELD | yield() | Voluntary context switch |
| 4 | SYS_EXEC | exec(filename) | Replace process with ELF |
| 5 | SYS_GFX_INIT | gfx_init() | Enter graphics mode |
| 6 | SYS_GFX_EXIT | gfx_exit() | Return to text mode |
| 7 | SYS_GETKEY | getkey(flags) | Keyboard input |
| 8 | SYS_SPAWN | spawn(file, argv, argc) | Create child process |
| 9 | SYS_WAIT | wait(pid) | Block until child exits |
| 10 | SYS_READDIR | readdir(idx, buf, size) | List directory entries |
| 11 | SYS_GETPID | getpid() | Get current process ID |
| 12 | SYS_TASKINFO | taskinfo() | Print task list |
| 13 | SYS_SHUTDOWN | shutdown() | ACPI power off |
| 14 | SYS_WIN_CREATE | win_create(w_h, title) | Create window |
| 15 | SYS_WIN_DESTROY | win_destroy(wid) | Destroy window |
| 16 | SYS_WIN_WRITE | win_write(wid, data, len) | Write pixels to window |
| 17 | SYS_WIN_READ | win_read(wid, dest, len) | Read pixels from window |
| 18 | SYS_WIN_GETKEY | win_getkey(wid) | Get key for window |
| 19 | SYS_WIN_SENDKEY | win_sendkey(wid, key) | Send key to window |
| 20 | SYS_WIN_LIST | win_list(out, max) | List windows |
| 21 | SYS_GFX_INFO | gfx_info() | Get screen dimensions |
| 22 | SYS_TASKLIST | tasklist(buf, max) | Structured task data |
| 23 | SYS_WAIT_NB | wait_nb(pid) | Non-blocking wait |
| 24 | SYS_PING | net_ping(ip, timeout) | ICMP ping |
| 25 | SYS_NETCFG | net_cfg(ip, mask, gw) | Set network config |
| 26 | SYS_NETGET | net_get(&ip, &mask, &gw) | Get network config |
| 27 | SYS_SLEEPMS | sleep_ms(ms) | Millisecond sleep |
| 28 | SYS_SOCK_LISTEN | sock_listen(port) | TCP listen |
| 29 | SYS_SOCK_ACCEPT | sock_accept(fd) | TCP accept |
| 30 | SYS_SOCK_SEND | sock_send(fd, buf, len) | TCP send |
| 31 | SYS_SOCK_RECV | sock_recv(fd, buf, len) | TCP receive |
| 32 | SYS_SOCK_CLOSE | sock_close(fd) | TCP close |
| 33 | SYS_WIN_READ_TEXT | win_read_text(wid, buf, max) | Read text from window |
| 34 | SYS_WIN_SET_STDOUT | win_set_stdout(wid) | Redirect stdout to window |
| 35 | SYS_GETMOUSE | getmouse(&x, &y, &btn) | Get mouse state |
| 36 | SYS_OPEN | open(path, flags) | Open file (VFS) |
| 37 | SYS_FREAD | fread(fd, buf, len) | Read file |
| 38 | SYS_FWRITE | fwrite(fd, buf, len) | Write file |
| 39 | SYS_CLOSE | close(fd) | Close file |
| 40 | SYS_SEEK | seek(fd, offset, whence) | Seek in file |
| 41 | SYS_STAT | stat(path, buf) | Get file info |

## Project Structure

### `src/`
Main kernel source files:
- `kernel.c` - Kernel entry point and initialization
- `task.c/h` - Task management, scheduler, per-process CR3 switching
- `syscall.c/h` - System call dispatcher and handlers (41 syscalls)
- `pmm.c/h` - Physical memory manager (bitmap frame allocator)
- `elf.c/h` - ELF32 binary loader
- `ramfs.c/h` - In-memory filesystem
- `vfs.c/h` - Virtual file system abstraction layer
- `multiboot.c/h` - Multiboot info parsing, initrd detection
- `console.c/h` - Early boot console
- `keyboard.c/h` - PS/2 keyboard driver with shift support and input buffering
- `net.c/h` - Networking layer (lwIP integration, TCP socket table, ICMP ping)
- `window.c/h` - Window manager subsystem (create, destroy, composite, focus)
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
- `mouse.c/h` - PS/2 mouse driver
- `boot.S` - Multiboot header and kernel entry

### `src/liballoc/`
Memory allocator (liballoc 1.1) with bump-allocator hooks

### `src/lwip/`
lwIP TCP/IP stack (NO_SYS=1 mode) providing ARP, IPv4, ICMP, TCP, UDP

### `rust/`
Rust components compiled with `no_std` and custom i686 target

### `userland/`
User-space programs:
- `shell.c` - Interactive shell with background job support and auto `.elf` extension
- `hello.c` - Hello world test program
- `test.c` - Comprehensive test suite (15 tests)
- `gui.c` - Window manager (compositing WM, 2x2 tiled layout)
- `winterm.c` - Terminal emulator running inside WM windows
- `winhello.c` - Window hello world demo
- `winedit.c` - Window text editor demo
- `winsleep.c` - Window sleep demo
- `httpd.c` - HTTP server (serves HTML on port 80)
- `ping.c` - ICMP ping utility (takes IP as argument)
- `cat.c` - Display file contents
- `echo.c` - Print arguments
- `ls.c` - List ramfs directory
- `tasks.c` - Show task list
- `ifconfig.c` - Network configuration
- `shutdown.c` - ACPI power off
- `ugfx.c/h` - Userland graphics library (pixel, rect, text, buffer ops)
- `syscalls.c/h` - Syscall wrappers (int 0x80, 41 syscalls)
- `cmd_shared.c/h` - Shared shell builtins (help, clear, exit)
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
- spawn() uses ecx=argv, edx=argc (0/0 defaults to argv={filename}, argc=1)

## License

Educational/experimental project.
