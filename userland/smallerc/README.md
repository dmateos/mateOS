# SmallerC Port (Phase 3)

This folder tracks the in-OS SmallerC bring-up work.

## Phase 3 Goal
- Keep SmallerC core running in mateOS.
- Support full in-OS compile flow through `cc.elf` (`smallerc -> as86 -> ld86`).
- Remove temporary pipeline hacks as assembler/linker mature.

## Runtime Prerequisites (implemented)
- Userspace program break syscall: `SYS_SBRK`.
- libc heap entry points backed by `sbrk`: `malloc/calloc/realloc/free`.
- `smallerc/compat_runtime.c` provides required libc/stdio surface for `smlrc.c`.
- Local minimal headers in `smallerc/include/` allow building without host headers.
- Vendored core sources in `smallerc/vendor/`:
  - `smlrc.c`
  - `fp.c`
  - `cgx86.c`
  - `cgmips.c`

## What Works Right Now
- `smallerc.elf` is built from upstream SmallerC core (`smlrc.c`).
- `smallerc_entry.c` forwards mateOS `_start` to SmallerC `main()`.
- `cc.elf` can build simple C programs in-OS using `smallerc.elf` through `as86 -f obj` + `ld86`.

## Current Temporary Integration
1. `cc.c` links prebuilt runtime objects (`crt0.o`, `libc.o`, `syscalls.o`) with app objects.
2. `as86` emits `MOBJ` objects (v2 symbols/relocs); `ld86` links `MOBJ` plus minimal ELF32 `.o` and simple `.a` archive inputs into ELF32.

## Next Steps
1. Upgrade object/link format beyond minimal ELF `.o` compatibility.
2. Evolve runtime packaging from direct object injection to standard libc/crt linkage policy.
3. Consolidate compiler/runtime headers into shared `userland/include`.
4. Add regression tests for return/print/rodata/externs and multi-file builds.
