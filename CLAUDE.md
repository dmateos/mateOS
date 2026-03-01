# mateOS Agent Guide

## Project Overview

mateOS is an educational 32-bit x86 operating system written in C and Rust. It boots via multiboot, runs on QEMU and real hardware (Proxmox), and features preemptive multitasking, a compositing window manager, TCP/IP networking, FAT16 filesystem, and 52 syscalls.

**This is a hobby/learning OS — keep it fun, keep it simple, keep it working.**

## Golden Rules

1. **Always update README.md** when adding or changing features, syscalls, commands, or build flags
2. **Always run `make clean && make`** after changes to verify zero build errors before declaring done
3. **Always update or add tests** in `userland/test.c` when adding new syscalls or kernel features
4. **Never break the boot** — after changes, verify shell launches and basic commands work (`hello`, `ls`, `test`)
5. **Keep the syscall table in README in sync** with `src/syscall.c` and `userland/syscalls.h`

## Best Practices

### Stability Over Features

- **Stability and memory safety come first.** A working OS with fewer features beats a broken OS with more. If a new feature risks destabilising existing functionality, fix the risk before shipping.
- **Validate all pointers and sizes from userland.** Syscall arguments come from untrusted Ring 3 code. Bounds-check buffer lengths, validate FD indices, reject out-of-range PIDs. Never trust user-supplied addresses without checking they fall in user-accessible pages.
- **Check every return value.** `pmm_alloc_frame()` can return 0 (OOM). VFS lookups can fail. Handle failure paths explicitly — a graceful error return is always better than a kernel panic or silent corruption.
- **Free what you allocate.** PMM frames, page tables, kernel stacks, FD tables — every allocation must have a corresponding free path in task exit/destroy. Leaked frames eventually exhaust the 6144-frame PMM.
- **Test after every change.** Run the 39-test suite (`test` in the shell). If you added a feature, add a test. If you fixed a bug, add a regression test.

### Think About the Whole System

- **Read before you write.** Before modifying a subsystem, read the files that interact with it. Changing `task.c`? Check `syscall.c`, `paging.c`, and `elf.c` for assumptions. Changing `vfs.c`? Check `fat16.c`, `vfs_proc.c`, and every userland program that does file I/O.
- **Consider all task states.** Code that touches `task_t` must handle: the task being NULL, the task having already exited, the task being the kernel idle task (PID 0), and the task being the currently running task. Spawn, wait, kill, and exit all interact — changing one may break another.
- **Consider both text and graphics mode.** Features should degrade gracefully. GUI programs print an error and exit if no WM is running. The kernel skips VGA text writes in BGA mode. Test in both `make run` and `make run GFX=1`.
- **Consider the FAT16 disk boundary.** File data is read from ATA PIO into kernel buffers before copying to user address spaces. VFS read paths handle this transparently.
- **Respect the memory map.** All addresses are meaningful. Don't allocate static buffers that push BSS past 0x400000. Don't map user pages into kernel-reserved regions. Use `src/memlayout.h` constants, not magic numbers.

### Good Abstractions

- **Use existing utilities.** `src/utils/strbuf.h` for safe string building, `src/utils/kring.h` for ring buffers, `src/utils/slot_table.h` for fixed-slot allocation. Don't reimplement these patterns.
- **Use the VFS layer.** New filesystems or data sources should register as VFS backends or virtual `.mos` files, not bypass VFS with custom syscalls. The `.mos` virtual file system replaced syscalls 46-49 (lspci, lsirq, meminfo, cpuinfo) with a cleaner file-based interface.
- **Put constants in the right place.** Memory layout in `memlayout.h`. Syscall IDs in `syscall.h` and `userland/syscalls.h`. Hardware ports in arch-specific headers. Max limits as `#define` near the data structures they govern.
- **Keep kernel and userland concerns separate.** Kernel code should not format output for human consumption — expose data via `.mos` files or structured syscalls, and let userland programs format it. The httpd `/os` page is a good example: kernel exposes raw data via `.mos` files, httpd formats it as HTML.
- **Extract reusable code.** If you write the same pattern twice, consider extracting it to `src/utils/`. The kring, strbuf, and slot_table modules all started as inline code that got reused.
- **Match existing style.** Look at surrounding code before writing new code. The codebase uses consistent patterns: `in_use` flags for slot tables, `static` for file-scoped state, `kprintf` for kernel debug output vs `printf` for user-visible output.

### Descriptive Comments

- **Comment the why, not the what.** `// Switch to kernel page tables for ATA PIO read because user pages aren't identity-mapped` is useful. `// Copy bytes` is not.
- **Comment every non-obvious design decision.** If code exists because of a hardware quirk, a bug workaround, or a subtle interaction between subsystems, say so. Future readers (including AI agents) will rely on these comments to avoid reintroducing bugs.
- **Comment struct fields and constants.** A `#define` or struct member should have a brief note if its purpose isn't obvious from the name: `uint16_t cluster; // First cluster number on FAT16 disk`.
- **Comment syscall handlers.** Each `case SYS_*:` block should have a one-line summary of what the syscall does and what its arguments are, especially when registers are used in non-obvious ways (e.g. packed width/height in ebx).
- **Comment tricky assembly.** `interrupts_asm.S` and `boot.S` contain subtle stack manipulation and register conventions. Every non-trivial instruction sequence should explain what state it expects and what state it leaves.
- **Comment magic numbers.** If a value like `0xE5` or `0x1CE` appears, explain it: `0xE5 = FAT16 deleted directory entry marker`, `0x01CE = BGA dispi index port`.
- **Use `// TODO:` for known gaps.** If something is incomplete or fragile, mark it so it's findable with grep.

## Build & Test

```bash
make clean && make           # Full rebuild — ALWAYS do this after changes
make run                     # Text mode — verify shell boots, run "test"
make run GFX=1               # Graphics mode — verify WM, winterm, winfm
make run NET=1 HTTP=1        # Networking — verify httpd, ping
```

### Testing Checklist

After any kernel or syscall change:
1. `make clean && make` — zero errors, zero warnings that aren't pre-existing
2. `make run` → shell boots → type `test` → all 39 tests pass
3. `make run` → type `ls` → files listed → type `hello` → prints hello
4. If GUI changed: `make run GFX=1` → `gui` → winterm works, winfm works
5. If networking changed: `make run NET=1 HTTP=1` → `httpd &` → `curl localhost:8080/os` from host

### Adding a Test

Tests live in `userland/test.c`. Pattern:
```c
static int test_my_feature(void) {
    print("TEST N: my feature\n");
    // ... test logic ...
    if (failed) { print("  FAIL: reason\n\n"); return 0; }
    print("  PASS\n\n");
    return 1;
}
```
Add it to the `tests[]` array and update `NUM_TESTS`. Update the test list in README.md.

## File Extensions

- `.elf` — CLI programs (shell, ls, cat, ping, etc.)
- `.wlf` — Window/GUI programs (winterm, winfm, wintask, winhello, etc.)
- `.mos` — Virtual OS interface files under `/proc/` (kcpuinfo.mos, kmeminfo.mos, etc.)

Shell and winterm try `bin/<cmd>.elf` first, then `bin/<cmd>.wlf` fallback. The FAT16 boot disk packs both `*.elf` and `*.wlf` in `/bin/`.

## Architecture Quick Reference

### Memory Layout (see `src/memlayout.h`)

| Region | Address | Notes |
|--------|---------|-------|
| Kernel code | 0x100000-0x1FFFFF | Loaded by multiboot |
| Kernel BSS/tables | 0x200000-0x4FFFFF | GDT, IDT, 256 page tables, TSS |
| Kernel heap | 0x500000-0x6FFFFF | 2MB, liballoc bump allocator |
| User code | 0x400000+ (virtual) | Per-process, private page tables |
| User stack | 0xBFFF0000-0xBFFFFFFF (virtual) | 16 pages (64KB), top-down |
| PMM frames | 0x800000-up to 1GB | Auto-detected, bitmap allocator |

### Key Limits

- MAX_TASKS: 16
- Boot disk: FAT16 with /bin/ (executables) and /lib/ (CRT/libc)
- VFS_MAX_FDS_PER_TASK: 16
- MAX_WINDOWS: 16 (WM slots)
- FAT16_MAX_OPEN: 16
- PMM frames: up to 1GB (auto-detected)

### Syscall Convention

- `int 0x80`, eax=syscall#, ebx/ecx/edx=args, return in eax
- Handler in `src/syscall.c`, stubs in `src/arch/i686/interrupts_asm.S`
- Userland wrappers in `userland/syscalls.c/h`
- Current IDs: 1-52 (contiguous)

## Adding a New Syscall

1. Pick the next available ID (currently 53)
2. Add `#define SYS_MYNAME 53` to `src/syscall.h` and `userland/syscalls.h`
3. Add `case SYS_MYNAME:` handler in `src/syscall.c`
4. Add userland wrapper in `userland/syscalls.c`
5. Add a test in `userland/test.c`
6. Update README.md syscall table
7. `make clean && make && make run` → run `test`

## Adding a New Userland Program

1. Create `userland/myprogram.c` with `void _start(int argc, char **argv)`
2. Add build rule in `userland/Makefile`:
   - CLI program: `myprogram.elf: myprogram.o syscalls.o libc.o`
   - GUI program: `myprogram.wlf: myprogram.o ugfx.o syscalls.o libc.o`
3. Add to `PROGRAMS` list in `userland/Makefile`
4. Add to README.md standalone programs list
5. `make clean && make` → `make run` → test it

GUI programs should call `detach()` after `win_create()` to release the parent shell.

## Adding a Virtual .mos File

Virtual OS files are generated in `src/fs/vfs_proc.c`. Pattern:
```c
static int vgen_myinfo(char *buf, int cap) {
    int pos = 0;
    strbuf_append_cstr(buf, cap, &pos, "my data here\n");
    return pos;
}
```
Register in `vfs_proc_register_files()`:
```c
vfs_register_virtual_file("proc/kmyinfo.mos", vgen_myinfo_size, vgen_myinfo_read);
```
Use the `VGEN_WRAPPER(myinfo)` macro to generate the size/read wrappers.

Update README.md Virtual OS Files section.

## Key Source Files

| File | Purpose |
|------|---------|
| `src/kernel.c` | Boot sequence, main loop |
| `src/syscall.c` | Syscall dispatcher (52 handlers) |
| `src/proc/task.c` | Scheduler, spawn, wait, kill, exit |
| `src/proc/elf.c` | ELF32 loader and validator |
| `src/proc/pmm.c` | Physical memory manager |
| `src/fs/vfs.c` | VFS dispatch + virtual file plumbing |
| `src/fs/vfs_proc.c` | `.mos` file generators |
| `src/fs/fat16.c` | FAT16 filesystem driver |
| `src/net/net.c` | Networking (lwIP, sockets, ping) |
| `src/io/window.c` | Window manager kernel side |
| `src/memlayout.h` | Memory layout constants |
| `src/arch/i686/paging.c` | Page tables, COW, address spaces |
| `src/arch/i686/interrupts_asm.S` | ISR stubs, syscall entry, context switch |
| `userland/syscalls.h` | Syscall IDs + wrapper prototypes |
| `userland/test.c` | 39-test suite |
| `userland/gui.c` | Window manager |
| `userland/shell.c` | Shell (text mode) |
| `userland/winterm.c` | Terminal emulator (GUI mode) |

## Kernel Utilities (`src/utils/`)

Reusable helpers — use these instead of rolling your own:
- **strbuf** (`strbuf.h`) — safe bounded string formatting (append_cstr, append_dec_u32, append_hex_u32)
- **kring** (`kring.h`) — generic byte ring buffer (push, pop, used, empty)
- **slot_table** (`slot_table.h`) — find-free-slot in fixed arrays with `in_use` flag

## Common Pitfalls

- **Shared page tables**: User processes share kernel page tables 0-7. If you map user pages into the kernel's range (>0x800000), you'll corrupt the identity mapping. The COW check in `paging_map_page()` handles this — don't bypass it.
- **Stack on exit**: `task_exit_with_code()` runs on the kernel stack it's about to free. Kernel stack freeing is deferred to task slot reuse.
- **`_start` calling convention**: gcc expects `[ret_addr][argc][argv]` on the stack. The kernel places a dummy 0 return address before argc/argv.
- **Build order**: `make` builds userland first, then packs into FAT16 boot disk (`boot.img`), then kernel. If you add a new program, it's automatically included in `/bin/`. But `doom.elf` requires doomgeneric sources.

## Version System

- `tools/gen_version_header.sh` generates `src/version.h` at build time
- Contains semver, git hash, ABI version, build timestamp
- Exposed at runtime via `/proc/kversion.mos`
- Override with `make VERSION_MAJOR=X VERSION_MINOR=Y VERSION_PATCH=Z`
- Bump version when making significant changes

## Documentation Updates

When modifying the project, update these in order:
1. **README.md** — features, syscall table, programs list, project structure, test count
2. **Test suite** — add test in `userland/test.c`, update NUM_TESTS, update README test list
3. **Version** — bump patch for bug fixes, minor for features, major for breaking changes

The README is the primary documentation. It should always reflect the current state of the codebase. If you add a syscall, program, build flag, or kernel feature, the README must be updated in the same session.
