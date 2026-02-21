# TinyCC Port Status

This folder tracks TinyCC bring-up work for mateOS.

## Current status

- TinyCC source is expected from a local checkout (default: `/tmp/tinycc`).
- We now have a repeatable Phase 1 probe to measure ABI/header gaps against mateOS userland runtime.
- Phase 2 bootstrap completed: shared userspace headers plus libc shims now satisfy TinyCC host-object symbol/header expectations in the probe.
- Phase 3 integration completed: `tcc.elf` now builds in `userland/` from vendored TinyCC i386 sources.

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

## Build TinyCC

From repo root:

```sh
make -C userland tcc.elf
```

## Next steps

1. Run in-OS execution smoke tests for `tcc.elf` via shell and capture stable logs.
2. Add a `tcc-smoke` harness (spawn `tcc.elf`, compile a tiny C file, run result).
3. Replace weak placeholder stubs (`dlopen`, pthread/semaphore internals, `mmap` policy, setjmp) with real implementations as execution paths require.
4. Move from vendored ad-hoc import to scripted TinyCC source sync/update workflow.
