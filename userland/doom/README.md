# Doomgeneric On mateOS (Scaffold)

This tree now contains a mateOS backend for `doomgeneric`:

- `doom/doom_mateos_start.c`: userland entry point (`_start`) that runs the main tick loop.
- `doom/doomgeneric_mateos.c`: window/input/time backend (`DG_*`) using mateOS syscalls.

## What is implemented

- Renders Doom frames into a GUI window with `win_write(...)`.
- Maps keyboard input from `win_getkey(...)` to Doom keys.
- Uses `sleep_ms(...)` and `get_ticks()` for timing.
- Runs detached so it can be launched from GUI/file manager.

## What you still need

1. Place `doomgeneric` sources under:
   - `userland/doom/doomgeneric/doomgeneric/`
2. Ensure `doomgeneric.h`, `doomkeys.h`, and `doomgeneric.c` are present there.
3. Put a WAD in the filesystem (for now expected default is `DOOM1.WAD`).

## Build

From repo root:

```sh
make -C userland doom.elf
```

If `doomgeneric` source files are not present, the make target prints a helpful message and exits without building.

## Run

Launch from shell/file manager:

```sh
doom.elf
```

Or pass explicit iwad args if your launcher supports argv:

```sh
doom.elf -iwad DOOM1.WAD
```
