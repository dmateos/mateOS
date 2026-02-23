void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick(void);

static inline int sc0(unsigned int n) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static inline int sc2(unsigned int n, unsigned int a1, unsigned int a2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline int sc3(unsigned int n, unsigned int a1, unsigned int a2, unsigned int a3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static inline int sc1(unsigned int n, unsigned int a1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static inline void k_yield(void) {
    (void)sc0(3);
}

static inline void k_write(const char *s, unsigned int n) {
    (void)sc3(1, 1, (unsigned int)s, n);
}

static inline int k_open(const char *path, int flags) {
    return sc2(36, (unsigned int)path, (unsigned int)flags);
}

static inline void k_close(int fd) {
    (void)sc1(39, (unsigned int)fd);
}

static const char *pick_iwad(void) {
    static const char *names[] = {
        "DOOM1.WAD",
        "doom1.wad",
        "DOOM.WAD",
        "doom.wad",
        "FREEDOOM1.WAD",
        "freedoom1.wad",
        0
    };
    for (int i = 0; names[i]; i++) {
        int fd = k_open(names[i], 0);
        if (fd >= 0) {
            k_close(fd);
            return names[i];
        }
    }
    return "DOOM1.WAD";
}

void _start(int argc, char **argv) {
    static char *default_argv[] = {
        "doom.elf",
        "-iwad",
        "DOOM1.WAD",
        "-mb",
        "3",
        "-nosound",
        "-nomusic",
        "-nosfx",
        0
    };

    if (argc <= 1 || !argv) {
        default_argv[2] = (char *)pick_iwad();
        argc = 8;
        argv = default_argv;
    }

    doomgeneric_Create(argc, argv);

    while (1) {
        doomgeneric_Tick();
        k_yield();
    }
}
