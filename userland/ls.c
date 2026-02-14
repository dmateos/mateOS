#include "syscalls.h"
#include "libc.h"

static const char *file_ext(const char *name) {
    const char *dot = 0;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') dot = name + i + 1;
    }
    return dot ? dot : "";
}

static int cmp_alpha(const char *a, const char *b) {
    return strcmp(a, b);
}

static int cmp_ext_grouped(const char *a, const char *b) {
    const char *ea = file_ext(a);
    const char *eb = file_ext(b);
    int ec = strcmp(ea, eb);
    if (ec != 0) return ec;
    return strcmp(a, b);
}

static void swap_names(char a[32], char b[32]) {
    char t[32];
    memcpy(t, a, sizeof(t));
    memcpy(a, b, sizeof(t));
    memcpy(b, t, sizeof(t));
}

void _start(int argc, char **argv) {
    int by_ext = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--ext") == 0) {
            by_ext = 1;
        }
    }

    char names[256][32];
    int count = 0;
    while (count < 256 && readdir((unsigned int)count, names[count], sizeof(names[count])) > 0) {
        count++;
    }

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            int c = by_ext ? cmp_ext_grouped(names[i], names[j])
                           : cmp_alpha(names[i], names[j]);
            if (c > 0) {
                swap_names(names[i], names[j]);
            }
        }
    }

    for (int i = 0; i < count; i++) {
        print("  ");
        print(names[i]);
        print("\n");
    }
    if (count == 0) print("  (no files)\n");
    exit(0);
}
