// Comprehensive test program for mateOS userland
// Tests syscalls, process management, memory isolation, and user mode functionality

#include "syscalls.h"

// Helper to write a string
static void print(const char *str) {
    const char *p = str;
    int len = 0;
    while (*p++) len++;
    write(1, str, len);
}

// Helper to write a number
static void print_num(int n) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        write(1, "0", 1);
        return;
    }

    if (n < 0) {
        write(1, "-", 1);
        n = -n;
    }

    // Build string in reverse
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    // Print in correct order
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

// Print hex value
static void print_hex(unsigned int val) {
    char buf[9];
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    print("0x");
    print(buf);
}

// Simple memset
static void *my_memset(void *s, int c, unsigned int n) {
    unsigned char *p = (unsigned char *)s;
    for (unsigned int i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

// Simple strcmp
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

// Simple strlen
static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

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

    int len = my_strlen(str);
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
    my_memset(buf, 0xFF, 256);
    for (int i = 0; i < 256; i++) {
        if (buf[i] != 0xFF) { print("  FAILED: fill 0xFF\n"); return 0; }
    }
    my_memset(buf, 0, 256);
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

        if (my_strcmp(name, "shell.elf") == 0) found_shell = 1;
        if (my_strcmp(name, "hello.elf") == 0) found_hello = 1;
        if (my_strcmp(name, "test.elf") == 0)  found_test = 1;

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
// Entry point
// ============================================================
void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    print("========================================\n");
    print("  mateOS User Program Test Suite\n");
    print("========================================\n\n");

    int passed = 0;
    int total = 15;

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
