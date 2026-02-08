// Comprehensive test program for mateOS userland
// Tests syscalls, program execution, and user mode functionality

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

// Test 1: Basic syscall functionality
static int test_syscalls(void) {
    print("TEST 1: Basic syscalls\n");

    // Test write
    print("  - write(): ");
    int ret = write(1, "OK", 2);
    print("\n");
    if (ret != 2) {
        print("  FAILED: write returned ");
        print_num(ret);
        print("\n");
        return 0;
    }

    // Test yield
    print("  - yield(): ");
    yield();
    print("OK\n");

    print("  PASSED\n\n");
    return 1;
}

// Test 2: String operations
static int test_strings(void) {
    print("TEST 2: String operations\n");

    const char *str = "Hello, User Mode!";
    print("  - String: ");
    print(str);
    print("\n");

    // Test strlen
    const char *p = str;
    int len = 0;
    while (*p++) len++;

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

// Test 3: Math operations
static int test_math(void) {
    print("TEST 3: Math operations\n");

    int a = 42;
    int b = 58;
    int sum = a + b;

    print("  - Addition: ");
    print_num(a);
    print(" + ");
    print_num(b);
    print(" = ");
    print_num(sum);
    print("\n");

    if (sum != 100) {
        print("  FAILED: incorrect result\n");
        return 0;
    }

    int mult = a * 2;
    print("  - Multiplication: ");
    print_num(a);
    print(" * 2 = ");
    print_num(mult);
    print("\n");

    if (mult != 84) {
        print("  FAILED: incorrect result\n");
        return 0;
    }

    print("  PASSED\n\n");
    return 1;
}

// Test 4: Stack usage
static int test_stack(void) {
    print("TEST 4: Stack operations\n");

    // Test local variables
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

    // Verify values
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i * i) {
            print("  FAILED: incorrect array value\n");
            return 0;
        }
    }

    print("  PASSED\n\n");
    return 1;
}

// Test 5: Function calls
static int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

static int test_functions(void) {
    print("TEST 5: Function calls\n");

    int result = factorial(5);
    print("  - factorial(5) = ");
    print_num(result);
    print("\n");

    if (result != 120) {
        print("  FAILED: incorrect result\n");
        return 0;
    }

    print("  PASSED\n\n");
    return 1;
}

// Test 6: Global data access
static int global_counter = 0;
static const char *global_string = "Global data works!";

static int test_globals(void) {
    print("TEST 6: Global data\n");

    global_counter++;
    global_counter += 9;

    print("  - Counter: ");
    print_num(global_counter);
    print("\n");

    if (global_counter != 10) {
        print("  FAILED: incorrect counter value\n");
        return 0;
    }

    print("  - String: ");
    print(global_string);
    print("\n");

    print("  PASSED\n\n");
    return 1;
}

// Test 7: Multiple yields (cooperative multitasking)
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

// Test 8: Memory patterns
static int test_memory(void) {
    print("TEST 8: Memory patterns\n");

    // Test that we can read/write memory
    unsigned char buf[32];

    // Write pattern
    for (int i = 0; i < 32; i++) {
        buf[i] = (unsigned char)i;
    }

    // Verify pattern
    for (int i = 0; i < 32; i++) {
        if (buf[i] != (unsigned char)i) {
            print("  FAILED: memory corruption at index ");
            print_num(i);
            print("\n");
            return 0;
        }
    }

    print("  - Pattern verification: OK\n");
    print("  PASSED\n\n");
    return 1;
}

// Entry point
void _start(void) {
    print("========================================\n");
    print("  mateOS User Program Test Suite\n");
    print("========================================\n\n");

    int passed = 0;
    int total = 8;

    // Run all tests
    if (test_syscalls()) passed++;
    if (test_strings()) passed++;
    if (test_math()) passed++;
    if (test_stack()) passed++;
    if (test_functions()) passed++;
    if (test_globals()) passed++;
    if (test_yields()) passed++;
    if (test_memory()) passed++;

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
