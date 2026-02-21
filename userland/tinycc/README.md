# TinyCC Port (Phase 1)

This folder tracks TinyCC bring-up work for mateOS.

## Current status

- TinyCC source is expected from a local checkout (default: `/tmp/tinycc`).
- We now have a repeatable Phase 1 probe to measure ABI/header gaps against mateOS userland runtime.
- Phase 2 bootstrap completed: shared userspace headers plus libc shims now satisfy TinyCC host-object symbol/header expectations in the probe.
- No in-OS `tcc.elf` binary is integrated yet.

## Run the probe

From repo root:

```sh
make tinycc-phase1
```

Optional custom TinyCC path:

```sh
make tinycc-phase1 TINYCC_SRC=/path/to/tinycc
```

The command generates:

- `userland/tinycc/PHASE1_GAP.md`

The report contains:

- Missing libc/syscall symbols TinyCC expects.
- Header coverage gaps versus `userland/include + userland/smallerc/include`.
- Already-satisfied symbols from current `libc.o` and `syscalls.o`.

Current result:

- Missing headers: `0`
- Missing symbols: `0`

## Next steps

1. Add missing C headers to a shared userspace include set (not Smallerc-only).
2. Implement/shim missing runtime symbols in `userland/libc.c` (or split into a dedicated runtime shim).
3. Build a reduced TinyCC frontend target for mateOS (`-c`/`-S` first), then layer linking/runtime support.
4. Replace weak placeholder stubs (`dlopen`, pthread/semaphore internals, `mmap` policy, setjmp) with real implementations as needed by real TinyCC execution paths.
