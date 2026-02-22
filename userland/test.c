// Comprehensive test program for mateOS userland
// Tests syscalls, process management, memory isolation, and user mode functionality

#include "syscalls.h"
#include "libc.h"

// ============================================================
// Test 1: Basic syscall functionality
// ============================================================
static int test_syscalls(void) {
    print("TEST 1: Basic syscalls (write, yield)\n");

    // Test write returns correct byte count
    print("  - write(): ");
    int ret = write(1, "OK", 2);
    print("\n");
    if (ret != 2) {
        print("  FAILED: write returned ");
        print_num(ret);
        print(" (expected 2)\n");
        return 0;
    }

    // Test write with larger buffer
    const char *msg = "Hello, testing!";
    ret = write(1, msg, 0);  // zero-length write
    // Zero-length write should return -1 (invalid)
    // (kernel returns -1 for len==0)

    // Test yield doesn't crash
    print("  - yield(): ");
    yield();
    print("OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 2: String operations in userland
// ============================================================
static int test_strings(void) {
    print("TEST 2: String operations\n");

    const char *str = "Hello, User Mode!";
    print("  - String: ");
    print(str);
    print("\n");

    int len = strlen(str);
    print("  - Length: ");
    print_num(len);
    print("\n");

    if (len != 17) {
        print("  FAILED: incorrect length\n");
        return 0;
    }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 3: Math operations
// ============================================================
static int test_math(void) {
    print("TEST 3: Math operations\n");

    int a = 42, b = 58;
    int sum = a + b;
    print("  - Addition: ");
    print_num(a); print(" + "); print_num(b); print(" = "); print_num(sum);
    print("\n");
    if (sum != 100) { print("  FAILED\n"); return 0; }

    int mult = a * 2;
    print("  - Multiplication: ");
    print_num(a); print(" * 2 = "); print_num(mult);
    print("\n");
    if (mult != 84) { print("  FAILED\n"); return 0; }

    // Test division
    int div = 100 / 7;
    int mod = 100 % 7;
    print("  - Division: 100 / 7 = ");
    print_num(div); print(" remainder "); print_num(mod);
    print("\n");
    if (div != 14 || mod != 2) { print("  FAILED\n"); return 0; }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 4: Stack usage (local arrays)
// ============================================================
static int test_stack(void) {
    print("TEST 4: Stack operations\n");

    int arr[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = i * i;
    }

    print("  - Array: [");
    for (int i = 0; i < 10; i++) {
        print_num(arr[i]);
        if (i < 9) print(", ");
    }
    print("]\n");

    for (int i = 0; i < 10; i++) {
        if (arr[i] != i * i) {
            print("  FAILED: incorrect array value\n");
            return 0;
        }
    }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 5: Function calls (recursion)
// ============================================================
static int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

static int fibonacci(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

static int test_functions(void) {
    print("TEST 5: Function calls\n");

    int result = factorial(5);
    print("  - factorial(5) = ");
    print_num(result);
    print("\n");
    if (result != 120) { print("  FAILED\n"); return 0; }

    int fib = fibonacci(10);
    print("  - fibonacci(10) = ");
    print_num(fib);
    print("\n");
    if (fib != 55) { print("  FAILED\n"); return 0; }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 6: Global/BSS data access
// ============================================================
static int global_counter = 0;
static const char *global_string = "Global data works!";
static int bss_array[8];  // Should be zero-initialized

static int test_globals(void) {
    print("TEST 6: Global and BSS data\n");

    global_counter++;
    global_counter += 9;
    print("  - Counter: ");
    print_num(global_counter);
    print("\n");
    if (global_counter != 10) { print("  FAILED: counter\n"); return 0; }

    print("  - String: ");
    print(global_string);
    print("\n");

    // Verify BSS is zeroed
    print("  - BSS zero-init: ");
    for (int i = 0; i < 8; i++) {
        if (bss_array[i] != 0) {
            print("FAILED at index ");
            print_num(i);
            print("\n");
            return 0;
        }
    }
    print("OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 7: Multiple yields (cooperative scheduling)
// ============================================================
static int test_yields(void) {
    print("TEST 7: Multiple yields\n");

    print("  - Yielding 5 times...\n");
    for (int i = 0; i < 5; i++) {
        print("    Yield ");
        print_num(i + 1);
        print("\n");
        yield();
    }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 8: Memory patterns (stack buffer)
// ============================================================
static int test_memory(void) {
    print("TEST 8: Memory patterns\n");

    unsigned char buf[256];

    // Write ascending pattern
    for (int i = 0; i < 256; i++) {
        buf[i] = (unsigned char)i;
    }
    for (int i = 0; i < 256; i++) {
        if (buf[i] != (unsigned char)i) {
            print("  FAILED: ascending pattern at ");
            print_num(i);
            print("\n");
            return 0;
        }
    }
    print("  - Ascending pattern (256 bytes): OK\n");

    // Write 0xAA/0x55 alternating pattern
    for (int i = 0; i < 256; i++) {
        buf[i] = (i & 1) ? 0x55 : 0xAA;
    }
    for (int i = 0; i < 256; i++) {
        unsigned char expected = (i & 1) ? 0x55 : 0xAA;
        if (buf[i] != expected) {
            print("  FAILED: alternating pattern at ");
            print_num(i);
            print("\n");
            return 0;
        }
    }
    print("  - Alternating 0xAA/0x55 pattern: OK\n");

    // Fill with 0xFF then zero
    memset(buf, 0xFF, 256);
    for (int i = 0; i < 256; i++) {
        if (buf[i] != 0xFF) { print("  FAILED: fill 0xFF\n"); return 0; }
    }
    memset(buf, 0, 256);
    for (int i = 0; i < 256; i++) {
        if (buf[i] != 0) { print("  FAILED: fill 0x00\n"); return 0; }
    }
    print("  - Memset fill/zero: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 9: getpid syscall
// ============================================================
static int test_getpid(void) {
    print("TEST 9: getpid syscall\n");

    int pid = getpid();
    print("  - PID: ");
    print_num(pid);
    print("\n");

    if (pid <= 0) {
        print("  FAILED: invalid PID\n");
        return 0;
    }

    // Call again - should return same value
    int pid2 = getpid();
    if (pid != pid2) {
        print("  FAILED: PID changed between calls\n");
        return 0;
    }
    print("  - PID stable across calls: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 10: readdir syscall (ramfs directory listing)
// ============================================================
static int test_readdir(void) {
    print("TEST 10: readdir syscall\n");

    char name[32];
    int count = 0;
    int found_shell = 0;
    int found_hello = 0;
    int found_test = 0;

    while (readdir(count, name, sizeof(name)) > 0) {
        print("  - File ");
        print_num(count);
        print(": ");
        print(name);
        print("\n");

        if (strcmp(name, "shell.elf") == 0) found_shell = 1;
        if (strcmp(name, "hello.elf") == 0) found_hello = 1;
        if (strcmp(name, "test.elf") == 0)  found_test = 1;

        count++;
        if (count > 20) break;  // Safety limit
    }

    if (count == 0) {
        print("  FAILED: no files found\n");
        return 0;
    }
    print("  - Total files: ");
    print_num(count);
    print("\n");

    if (!found_shell) { print("  FAILED: shell.elf not found\n"); return 0; }
    if (!found_hello) { print("  FAILED: hello.elf not found\n"); return 0; }
    if (!found_test)  { print("  FAILED: test.elf not found\n"); return 0; }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 11: spawn + wait (process management)
// ============================================================
static int test_spawn_wait(void) {
    print("TEST 11: spawn + wait\n");

    // Spawn hello.elf as a child process
    print("  - Spawning hello.elf...\n");
    int child = spawn("hello.elf");
    if (child < 0) {
        print("  FAILED: spawn returned ");
        print_num(child);
        print("\n");
        return 0;
    }
    print("  - Child PID: ");
    print_num(child);
    print("\n");

    // Wait for child to finish
    int code = wait(child);
    print("  - Child exit code: ");
    print_num(code);
    print("\n");
    if (code != 0) {
        print("  FAILED: expected exit code 0\n");
        return 0;
    }

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 12: spawn invalid file (error handling)
// ============================================================
static int test_spawn_invalid(void) {
    print("TEST 12: spawn error handling\n");

    // Try to spawn a non-existent file
    print("  - Spawning nonexistent.elf...\n");
    int ret = spawn("nonexistent.elf");
    print("  - Result: ");
    print_num(ret);
    print("\n");

    if (ret >= 0) {
        print("  FAILED: should have returned error\n");
        return 0;
    }
    print("  - Correctly returned error for missing file\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 13: write return value validation
// ============================================================
static int test_write_return(void) {
    print("TEST 13: write return values\n");

    // Write known lengths and verify return
    int ret1 = write(1, "A", 1);
    print("\n");
    if (ret1 != 1) {
        print("  FAILED: write(1 byte) returned ");
        print_num(ret1);
        print("\n");
        return 0;
    }
    print("  - write(1 byte) = 1: OK\n");

    const char *msg = "Hello!";
    int ret6 = write(1, msg, 6);
    print("\n");
    if (ret6 != 6) {
        print("  FAILED: write(6 bytes) returned ");
        print_num(ret6);
        print("\n");
        return 0;
    }
    print("  - write(6 bytes) = 6: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 14: Large stack usage (deep recursion, big locals)
// ============================================================
static int sum_recursive(int n) {
    if (n <= 0) return 0;
    // Use some stack space with a local array
    volatile int pad[4];
    pad[0] = n;
    return pad[0] + sum_recursive(n - 1);
}

static int test_deep_stack(void) {
    print("TEST 14: Deep stack usage\n");

    // Recursive sum 1+2+...+50 = 1275, using stack frames with padding
    int result = sum_recursive(50);
    print("  - sum(1..50) = ");
    print_num(result);
    print("\n");
    if (result != 1275) {
        print("  FAILED: expected 1275\n");
        return 0;
    }

    // Large local array
    int big[128];
    for (int i = 0; i < 128; i++) {
        big[i] = i * 3 + 7;
    }
    int check = 1;
    for (int i = 0; i < 128; i++) {
        if (big[i] != i * 3 + 7) { check = 0; break; }
    }
    print("  - Large local array (128 ints): ");
    print(check ? "OK" : "FAILED");
    print("\n");
    if (!check) return 0;

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 15: Process isolation (spawn and verify we survive)
// ============================================================
static volatile int isolation_marker = 0xDEAD;

static int test_process_isolation(void) {
    print("TEST 15: Process isolation\n");

    // Set a marker in our globals
    isolation_marker = 0xBEEF;

    // Spawn hello.elf - it will load at 0x700000 in its OWN address space
    print("  - Marker before spawn: ");
    print_hex(isolation_marker);
    print("\n");

    int child = spawn("hello.elf");
    if (child < 0) {
        print("  FAILED: spawn failed\n");
        return 0;
    }
    int code = wait(child);

    // Check our marker is still intact
    print("  - Marker after child exit: ");
    print_hex(isolation_marker);
    print("\n");

    if (isolation_marker != 0xBEEF) {
        print("  FAILED: marker corrupted by child process!\n");
        return 0;
    }
    print("  - Process memory isolation: OK\n");

    // Spawn again to doubly verify
    child = spawn("hello.elf");
    if (child >= 0) {
        wait(child);
    }
    if (isolation_marker != 0xBEEF) {
        print("  FAILED: marker corrupted on second spawn!\n");
        return 0;
    }
    print("  - Second spawn isolation: OK\n");

    (void)code;
    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 16: Additional libc coverage (strncmp, memcpy, itoa)
// ============================================================
static int test_libc_more(void) {
    print("TEST 16: libc helpers (strncmp, memcpy, itoa)\n");

    if (strncmp("abcdef", "abcxyz", 3) != 0) {
        print("  FAILED: strncmp prefix compare\n");
        return 0;
    }
    if (strncmp("abc", "abd", 3) >= 0) {
        print("  FAILED: strncmp ordering\n");
        return 0;
    }
    print("  - strncmp: OK\n");

    unsigned char src[8] = {0x10, 0x20, 0x30, 0x40, 0xAA, 0xBB, 0xCC, 0xDD};
    unsigned char dst[8];
    memset(dst, 0, sizeof(dst));
    memcpy(dst, src, sizeof(src));
    for (int i = 0; i < 8; i++) {
        if (dst[i] != src[i]) {
            print("  FAILED: memcpy mismatch at ");
            print_num(i);
            print("\n");
            return 0;
        }
    }
    print("  - memcpy: OK\n");

    char numbuf[16];
    itoa(0, numbuf);
    if (strcmp(numbuf, "0") != 0) { print("  FAILED: itoa(0)\n"); return 0; }
    itoa(12345, numbuf);
    if (strcmp(numbuf, "12345") != 0) { print("  FAILED: itoa(12345)\n"); return 0; }
    itoa(-42, numbuf);
    if (strcmp(numbuf, "-42") != 0) { print("  FAILED: itoa(-42)\n"); return 0; }
    print("  - itoa: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 17: wait_nb syscall
// ============================================================
static int test_wait_nb(void) {
    print("TEST 17: wait_nb syscall\n");

    // Deterministic running-task path
    int self_state = wait_nb(getpid());
    if (self_state != -1) {
        print("  FAILED: wait_nb(self) expected -1, got ");
        print_num(self_state);
        print("\n");
        return 0;
    }
    print("  - wait_nb(self) while running: OK\n");

    // Child completion path
    const char *argv[] = {"echo.elf", "wait_nb", "test", 0};
    int child = spawn_argv("echo.elf", argv, 3);
    if (child < 0) {
        print("  FAILED: spawn_argv(echo.elf)\n");
        return 0;
    }
    int code = -1;
    for (int i = 0; i < 500; i++) {
        code = wait_nb(child);
        if (code != -1) break;
        yield();
    }
    if (code != 0) {
        print("  FAILED: child completion code ");
        print_num(code);
        print(" (expected 0)\n");
        return 0;
    }
    print("  - wait_nb(child) completion: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 18: sleep_ms syscall
// ============================================================
static int test_sleep_ms(void) {
    print("TEST 18: sleep_ms syscall\n");
    print("  - sleeping 25ms...\n");
    int ret = sleep_ms(25);
    if (ret != 0) {
        print("  FAILED: sleep_ms returned ");
        print_num(ret);
        print("\n");
        return 0;
    }
    print("  - resumed after sleep: OK\n");
    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 19: tasklist syscall
// ============================================================
static int test_tasklist(void) {
    print("TEST 19: tasklist syscall\n");

    taskinfo_entry_t entries[16];
    memset(entries, 0, sizeof(entries));

    int count = tasklist(entries, 16);
    if (count <= 0) {
        print("  FAILED: tasklist count ");
        print_num(count);
        print("\n");
        return 0;
    }
    print("  - task count: ");
    print_num(count);
    print("\n");

    int self = getpid();
    int found = 0;
    for (int i = 0; i < count; i++) {
        if ((int)entries[i].id == self) {
            found = 1;
            if (entries[i].state > 3) {
                print("  FAILED: invalid self state ");
                print_num(entries[i].state);
                print("\n");
                return 0;
            }
            if (entries[i].name[0] == '\0') {
                print("  FAILED: empty self task name\n");
                return 0;
            }
            break;
        }
    }
    if (!found) {
        print("  FAILED: self PID not present in tasklist\n");
        return 0;
    }
    print("  - self PID present: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 20: detach behavior (best-effort using existing detached app)
// ============================================================
static int test_detach(void) {
    print("TEST 20: detach behavior\n");

    // Existing detached app in tree: winsleep.wlf (detaches after win_create).
    // In text mode (no WM), it exits before detach, so we treat that as skipped.
    int child = spawn("winsleep.wlf");
    if (child < 0) {
        print("  SKIP: couldn't spawn winsleep.wlf\n\n");
        return 1;
    }

    int code = wait(child);
    if (code == -3) {
        print("  - wait() returned -3 for detached child: OK\n");
        print("  PASSED\n\n");
        return 1;
    }

    print("  SKIP: winsleep exited without detaching (likely no WM), code=");
    print_num(code);
    print("\n\n");
    return 1;
}

// ============================================================
// Test 21: VFS file I/O (open/read/seek/close/stat)
// ============================================================
static int test_vfs_io(void) {
    print("TEST 21: VFS file I/O\n");

    int fd = open("hello.elf", 0);  // O_RDONLY
    if (fd < 0) {
        print("  FAILED: open hello.elf\n");
        return 0;
    }

    unsigned char hdr[4];
    int n = fd_read(fd, hdr, 4);
    if (n != 4) {
        print("  FAILED: fread header bytes=");
        print_num(n);
        print("\n");
        close(fd);
        return 0;
    }
    if (hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
        print("  FAILED: ELF magic mismatch\n");
        close(fd);
        return 0;
    }
    print("  - ELF magic check: OK\n");

    int pos = seek(fd, 0, SEEK_SET);
    if (pos != 0) {
        print("  FAILED: seek(SET,0) returned ");
        print_num(pos);
        print("\n");
        close(fd);
        return 0;
    }
    unsigned char b0 = 0;
    n = fd_read(fd, &b0, 1);
    if (n != 1 || b0 != 0x7F) {
        print("  FAILED: seek+read verification\n");
        close(fd);
        return 0;
    }
    print("  - seek+readback: OK\n");

    stat_t st;
    if (stat("hello.elf", &st) != 0) {
        print("  FAILED: stat hello.elf\n");
        close(fd);
        return 0;
    }
    if (st.size == 0 || st.type != 0) {
        print("  FAILED: stat fields invalid\n");
        close(fd);
        return 0;
    }
    print("  - stat size/type: OK\n");

    if (close(fd) != 0) {
        print("  FAILED: close\n");
        return 0;
    }
    print("  - close: OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 22: spawn_argv syscall
// ============================================================
static int test_spawn_argv(void) {
    print("TEST 22: spawn_argv syscall\n");
    const char *argv[] = {"echo.elf", "arg1", "arg2", "arg3", 0};
    int child = spawn_argv("echo.elf", argv, 4);
    if (child < 0) {
        print("  FAILED: spawn_argv returned ");
        print_num(child);
        print("\n");
        return 0;
    }
    int code = wait(child);
    if (code != 0) {
        print("  FAILED: child exit code ");
        print_num(code);
        print("\n");
        return 0;
    }
    print("  - child ran with argv and exited 0: OK\n");
    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Test 23: write edge cases
// ============================================================
static int test_write_edges(void) {
    print("TEST 23: write edge cases\n");

    int r = write(1, "Z", 0);
    if (r != -1) {
        print("  FAILED: write(len=0) returned ");
        print_num(r);
        print(" (expected -1)\n");
        return 0;
    }
    print("  - write(len=0): OK\n");

    r = write(1, (const void *)0, 1);
    if (r != -1) {
        print("  FAILED: write(NULL,1) returned ");
        print_num(r);
        print(" (expected -1)\n");
        return 0;
    }
    print("  - write(NULL,1): OK\n");

    // Current kernel accepts fd != 1 and still writes to console.
    r = write(2, "E", 1);
    print("\n");
    if (r != 1) {
        print("  FAILED: write(fd=2,1) returned ");
        print_num(r);
        print(" (expected 1)\n");
        return 0;
    }
    print("  - write(fd=2,1): OK\n");

    print("  PASSED\n\n");
    return 1;
}

// ============================================================
// Entry point
// ============================================================
void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    print("========================================\n");
    print("  mateOS User Program Test Suite\n");
    print("========================================\n\n");

    int passed = 0;
    int total = 23;

    // Run all tests
    if (test_syscalls())         passed++;  // 1
    if (test_strings())          passed++;  // 2
    if (test_math())             passed++;  // 3
    if (test_stack())            passed++;  // 4
    if (test_functions())        passed++;  // 5
    if (test_globals())          passed++;  // 6
    if (test_yields())           passed++;  // 7
    if (test_memory())           passed++;  // 8
    if (test_getpid())           passed++;  // 9
    if (test_readdir())          passed++;  // 10
    if (test_spawn_wait())       passed++;  // 11
    if (test_spawn_invalid())    passed++;  // 12
    if (test_write_return())     passed++;  // 13
    if (test_deep_stack())       passed++;  // 14
    if (test_process_isolation()) passed++; // 15
    if (test_libc_more())        passed++;  // 16
    if (test_wait_nb())          passed++;  // 17
    if (test_sleep_ms())         passed++;  // 18
    if (test_tasklist())         passed++;  // 19
    if (test_detach())           passed++;  // 20
    if (test_vfs_io())           passed++;  // 21
    if (test_spawn_argv())       passed++;  // 22
    if (test_write_edges())      passed++;  // 23

    print("========================================\n");
    print("  Results: ");
    print_num(passed);
    print("/");
    print_num(total);
    print(" tests passed\n");
    print("========================================\n\n");

    if (passed == total) {
        print("SUCCESS: All tests passed!\n");
        exit(0);
    } else {
        print("FAILURE: Some tests failed!\n");
        exit(1);
    }
}
