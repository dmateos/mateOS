# Compiler Status (mateOS)

This document tracks the **current** compiler/toolchain state in mateOS and the remaining work to make it proper (no hacks).

## Current Status

### Working now
- `smallerc.elf` runs in mateOS and emits x86 asm.
- `as86.elf` assembles the current SmallerC output subset to flat binary.
- `ld86.elf` wraps flat binary into ELF32 (single `PT_LOAD`).
- `cc.elf` drives the full in-OS pipeline:
  - `smallerc -> as86 -> ld86`
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
- Added initial `as86 -f obj` container format (`MOBJ` v1) and `ld86` support for consuming it.

## Current Hacks / Technical Debt

1. `cc.c` rewrites generated asm by hand.
- Injects built-in crt0 (`$_start`) and built-in `$print`.

2. `as86.c` is still a flat assembler subset.
- Has real `.text/.rodata/.data/.bss` tracking and deterministic flat layout.
- No relocatable object output.
- Two-pass sizing still uses a forward-label placeholder heuristic.

3. `ld86.c` is a packer, not a full linker.
- Wraps one flat binary into one ELF segment.
- No multi-object linking, relocations, symbol resolution.

4. Runtime/libc integration is temporary.
- `smallerc/compat_runtime.c` exists mainly to get SmallerC running.
- Proper shared userland include/lib split is not finished.

## Next Steps (Priority Order)

1. Replace remaining asm text surgery in `cc`.
- Remove remaining runtime injection hacks.

2. Complete relocatable object support.
- Extend `as86 -f obj` from container-only to full symbols + relocation records.
- Resolve extern/global references during link.

3. Turn `ld86` into a real linker.
- Multi-input `.o`, symbol resolution, reloc application, final ELF emit.

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
- `app.elf`

`cc` now removes temp files by default.
- Use `--keep-temps` to keep `cc_<pid>.asm` and `cc_<pid>.bin` for debugging.
