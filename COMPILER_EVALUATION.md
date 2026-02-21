# Compiler Status (mateOS)

This document tracks the **current** compiler/toolchain state in mateOS and the remaining work to make it proper (no hacks).

## Current Status

### Working now
- `smallerc.elf` runs in mateOS and emits x86 asm.
- `as86.elf` supports `-f bin` and `-f obj` (`MOBJ` v2 with symbols+relocs).
- `ld86.elf` links flat binaries, `MOBJ` objects, minimal ELF32 relocatable `.o` inputs, and simple `.a` archives into ELF32 (single `PT_LOAD`).
- `cc.elf` drives the full in-OS pipeline:
  - `smallerc -> as86 -f obj -> ld86`
- `cc.elf` now supports multi-file compile/link:
  - `cc a.c b.c -o app.elf`
- `cc.elf` now supports compile-driver modes:
  - `-S` (emit asm), `-c` (emit object), and direct object/archive linking.
- Basic programs now build and run in-OS:
  - `return 0;`
  - `print("..."); return 0;`

### What this means
- End-to-end in-OS C compilation is now functional for simple programs.
- This is **phase-1 functional**, not yet a clean toolchain architecture.

### Recently Completed
- Implemented real section-aware flat layout in `as86` for `.text/.rodata/.data/.bss`.
- Removed `cc` section-reorder hack.
- Added `cc` temp-file cleanup by default (`--keep-temps` opt-in).
- Verified in-OS smoke tests: return-only and print cases.
- Added `as86 -f obj` `MOBJ` v2 with symbol and relocation tables.
- Added relocation application in `ld86` for single-object `MOBJ` links.
- Added multi-input linking in `ld86` with cross-object global symbol resolution.
- Added minimal ELF32 `.o` ingestion in `ld86` (alloc sections + symtab + `R_386_32`/`R_386_PC32` relocations).
- Added simple `.a` archive expansion in `ld86`.
- Switched `cc` runtime to prebuilt objects (`crt0.o`, `libc.o`, `syscalls.o`) and removed runtime asm shims.
- Added multi-file input support in `cc`.
- Added `cc` driver modes (`-S`, `-c`) and mixed input linking (`.c`, `.o/.obj`, `.a`).

## Current Hacks / Technical Debt

1. Runtime model is still transitional.
- `cc` directly injects runtime object set (`crt0.o`, `libc.o`, `syscalls.o`) on link.
- Not yet using a full installed libc/crt packaging model with policy-driven lib resolution.

2. `as86.c` is still a subset assembler.
- Has real `.text/.rodata/.data/.bss` tracking and deterministic flat layout.
- Emits relocatable `MOBJ` objects (v2), but relocation coverage is still incomplete.
- Two-pass sizing still uses a forward-label placeholder heuristic.

3. `ld86.c` is still not a full linker.
- Supports multi-input objects and applies core ABS32/REL32 relocations.
- Has minimal ELF `.o` ingestion and simple archive expansion, but still no section GC and minimal diagnostics.

4. Runtime/libc integration is temporary.
- `smallerc/compat_runtime.c` exists mainly to get SmallerC running.
- Proper shared userland include/lib split is not finished.

## Next Steps (Priority Order)

1. Complete relocatable object support.
- Add reloc coverage for remaining instruction/data edge cases.
 - Add stronger duplicate/visibility diagnostics.

2. Turn `ld86` into a real linker.
- Expand ELF/archive support and add richer link diagnostics.
- Add deterministic library symbol resolution policy.

3. Harden `cc` driver behavior.
- Better option diagnostics and error-path reporting.
- Optional dependency tracking/compile caching.

4. Harden and test.
- Regression suite for: return-only, print, rodata labels, extern calls, multi-file.
- Better diagnostics for assembler/linker symbol failures.

## Practical Usage (Current)

Inside mateOS:
- `cc test2.c -o app.elf` (return-only sample)
- `cc test.c -o app.elf` (print sample)
- `cc t3a.c t3b.c -o app.elf` (multi-file sample)
- `cc -S test2.c -o test2.asm` (asm output)
- `cc -c test2.c -o test2.o` (object output)
- `cc t4.c libtiny.a -o app.elf` (archive link sample)
- `app.elf`

`cc` now removes temp files by default.
- Use `--keep-temps` to keep `cc_<pid>.asm` and `cc_<pid>.obj` for debugging.
- Fast host-side parser sanity check:
  - `make ld86-host-check`
