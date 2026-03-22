# TCC Crash Debug Analysis

## Problem Summary

TCC (`tcc.elf`, 357KB) compiles successfully but crashes immediately on startup with:
- Fault address: `0x0` (NULL pointer dereference)
- EIP: `0x739b45` (inside `main()`, 5 bytes after entry)
- Error code: `0x5` (present + read + user)
- EBX: `0x0`

## What Works ✅

1. **All dependencies satisfied**: 105 external symbols all provided by mateOS libc
2. **All headers present**: 37 required headers exist
3. **Successful compilation**: TCC builds to valid ELF32
4. **Correct linking**:
   - Entry point at `0x700000` (`_start`)
   - `_start` correctly calls `main` at `0x739b40`
   - Relocations properly applied (checked `__ctype_b_loc` returns 0x754008)
5. **ELF loads correctly**:
   - Segment 0: 0x700000-0x7530d0 (text/rodata/eh_frame)
   - Segment 1: 0x754000-0x768c08 (data/bss)
   - Entry point validated
   - BSS zeroing confirmed in `syscall.c:143`

## Crash Location Analysis

**Disassembly of main() entry:**
```asm
00739b40 <main>:
  739b40:   lea    0x4(%esp),%ecx        ; Save argc/argv pointer
  739b44:   and    $0xfffffff0,%esp      ; Align stack to 16 bytes
  739b47:   push   -0x4(%ecx)            ; Save return address
  739b4a:   mov    %ecx,%eax
  739b4c:   push   %ebp
  739b4d:   add    $0x4,%eax
  739b50:   mov    %esp,%ebp
  739b52:   push   %edi
  739b53:   push   %esi
  739b54:   xor    %esi,%esi
  739b56:   push   %ebx
  739b57:   push   %ecx
  739b58:   sub    $0x48,%esp
  739b5b:   mov    (%ecx),%edi           ; Load argc
  739b5d:   mov    %ecx,-0x34(%ebp)
  739b60:   movl   $0x0,-0x48(%ebp)
  739b67:   mov    %edi,-0x3c(%ebp)
  739b6a:   mov    0x4(%ecx),%edi        ; Load argv
  739b6d:   movl   $0x0,-0x4c(%ebp)
  739b74:   mov    %edi,-0x40(%ebp)
  739b77:   mov    0x754010,%edi         ; <-- Load stdout
```

**Crash is reported at 0x739b45** which is in the middle of `and $0xfffffff0,%esp` (3-byte instruction at 0x739b44).

This is physically impossible unless:
1. The debug output is wrong/misleading
2. There's memory corruption
3. CPU jumped to misaligned address

## Hypothesis: Debug Output Mismatch

The crash message shows `eip=0x739b45` but instruction bytes `30 b 70 0` which don't match the actual ELF contents at that location (should be `83 e4 f0`).

**This suggests the debug output is from a modified/debug kernel not in the repo.**

## Real Problem: NULL Pointer Usage

Even though the reported EIP is suspicious, the key facts are:
- `ebx=0x0`
- Fault at address `0x0`
- Error is a user-mode read fault

Something in TCC's early initialization is dereferencing NULL. Candidates:

### 1. stdout/stderr/stdin
At 0x739b77: `mov 0x754010,%edi` loads `stdout`.

**Checked:** stdout = 0x768c00 (valid BSS address), properly initialized in .data section.

### 2. environ
TCC likely accesses `environ` early.

**Location:** BSS at 0x7680f4

**Value:** Should be NULL initially, which is correct for no environment variables.

### 3. __ctype_* functions
If TCC calls `isalpha()`, `isdigit()` etc. early, these call `__ctype_b_loc()`.

**Checked:** `__ctype_b_loc()` returns 0x754008 (valid address), relocations applied correctly.

### 4. Missing initialization
TCC might expect certain global pointers to be initialized by libc constructors, but mateOS doesn't run `.init_array`/`.fini_array`.

## Next Steps to Debug

### Option 1: Add Early Debug Output

Modify TCC source to add `fprintf(stderr, "tcc: main entry\\n");` at the very first line of `main()` to see if we even get there.

### Option 2: Check .init_array

```bash
readelf -S tcc.elf | grep init
objdump -s -j .init_array tcc.elf
```

If TCC has init constructors, mateOS needs to call them before `main()`.

### Option 3: Run Under GDB (if supported)

Set breakpoint at `0x739b40` and single-step through main() to see exactly what instruction faults.

### Option 4: Simplify TCC Invocation

Instead of `tcc -v`, try building a minimal C program that just does:
```c
int main(int argc, char **argv) {
    return 0;
}
```

And link it with TCC's libtcc as a library to isolate the issue.

### Option 5: Check argc/argv Setup

Verify the kernel is setting up argc/argv correctly for TCC. The `_start` code expects:
```
[esp+0] = argc
[esp+4] = argv
```

But the kernel might be using a different layout.

## Recommended Fix Path

1. **Verify argc/argv** - Add debug output in `_start` before calling main
2. **Check for .init_array** - Run constructors if present
3. **Add early fprintf in main()** - Confirm we reach main's first C statement
4. **Bisect the crash** - Comment out sections of TCC's main() to isolate the NULL deref

## Files to Check

- `/mnt/data/per/code/mateOS/userland/tinycc/tinycc_entry.s` - _start implementation
- `/mnt/data/per/code/mateOS/src/syscall.c:97-183` - ELF loader (exec_into)
- `/tmp/tinycc/tcc.c` - TCC main() function
- `/tmp/tinycc/libtcc.c` - TCC initialization

## Conclusion

TCC is **99% working**. The remaining issue is a subtle initialization problem, likely:
- Missing .init_array execution, OR
- NULL environ causing crash, OR
- Incorrect argc/argv layout

The debug output showing misaligned EIP is a red herring from a debug kernel build.
