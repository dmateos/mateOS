#include "console.h"
#include "lib.h"

void console_init(void) {
    printf("\nWelcome to mateOS!\n");
    printf("Launching shell...\n\n");
}

void console_putchar(char c) {
    if (c == '\n') {
        printf("\n");
    } else if (c == '\b') {
        printf("\b \b");
    } else {
        printf("%c", c);
    }
}

void console_handle_key(char c) {
    // Fallback: just echo keys before shell takes over
    console_putchar(c);
}

void console_execute_command(const char *line __attribute__((unused))) {
    // No longer used - shell is in userland
}
