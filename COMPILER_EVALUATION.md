# Compiler Status (mateOS)

This document tracks the **current** compiler/toolchain state in mateOS and the remaining work to make it proper (no hacks).

## Current Status

### Working now
- `smallerc.elf` runs in mateOS and emits x86 asm.
- `as86.elf` supports `-f bin` and `-f obj` (`MOBJ` v2 with symbols+relocs).
- `ld86.elf` links flat binaries, `MOBJ` objects, and minimal ELF32 relocatable `.o` inputs into ELF32 (single `PT_LOAD`).
- `cc.elf` drives the full in-OS pipeline:
  - `smallerc -> as86 -f obj -> ld86`
- `cc.elf` now supports multi-file compile/link:
  - `cc a.c b.c -o app.elf`
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
- Switched `cc` runtime to prebuilt objects (`crt0.o` + `cprint.o`) and removed runtime asm shims.
- Added multi-file input support in `cc`.

## Current Hacks / Technical Debt

1. `cc.c` still has temporary runtime handling.
- Uses prebuilt runtime objects (`crt0.o`, `cprint.o`) instead of a normal libc/crt link model.
- Not yet using reusable runtime library objects.

2. `as86.c` is still a subset assembler.
- Has real `.text/.rodata/.data/.bss` tracking and deterministic flat layout.
- Emits relocatable `MOBJ` objects (v2), but relocation coverage is still incomplete.
- Two-pass sizing still uses a forward-label placeholder heuristic.

3. `ld86.c` is still not a full linker.
- Supports multi-input objects and applies core ABS32/REL32 relocations.
- Has minimal ELF `.o` ingestion, but still no archive libs, no section GC, and minimal diagnostics.

4. Runtime/libc integration is temporary.
- `smallerc/compat_runtime.c` exists mainly to get SmallerC running.
- Proper shared userland include/lib split is not finished.

## Next Steps (Priority Order)

1. Replace remaining temporary runtime handling in `cc`.
- Replace temporary asm/object runtime pair with reusable runtime libs (`crt0.o`, libc objs).

2. Complete relocatable object support.
- Add reloc coverage for remaining instruction/data edge cases.
 - Add stronger duplicate/visibility diagnostics.

3. Turn `ld86` into a real linker.
- Support standard ELF `.o` inputs, archives, and richer link diagnostics.

4. Add proper runtime objects.
- `crt0.o` for `_start`/exit path.
- libc objects for `print`/I/O and shared helpers.
- Link these normally instead of injected asm strings.

5. Harden and test.
- Regression suite for: return-only, print, rodata labels, extern calls, multi-file.
- Better diagnostics for assembler/linker symbol failures.

## Practical Usage (Current)

Inside mateOS:
- `cc test2.c -o app.elf` (return-only sample)
- `cc test.c -o app.elf` (print sample)
- `cc t3a.c t3b.c -o app.elf` (multi-file sample)
- `app.elf`

`cc` now removes temp files by default.
- Use `--keep-temps` to keep `cc_<pid>.asm` and `cc_<pid>.obj` for debugging.
