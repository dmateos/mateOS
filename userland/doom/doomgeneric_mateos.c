#include "doomgeneric.h"
#include "doomkeys.h"
#include <string.h>

typedef unsigned char uint8_t;

#define KEYQ_CAP 64

extern pixel_t* DG_ScreenBuffer;

static int g_wid = -1;
static int g_headless = 0;
static uint8_t g_fb8[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
static int g_keyq[KEYQ_CAP];
static int g_qhead = 0;
static int g_qtail = 0;

#define KEY_LEFT  0x80
#define KEY_RIGHT 0x81
#define KEY_UP    0x82
#define KEY_DOWN  0x83

static inline int sc0(unsigned int n) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static inline int sc1(unsigned int n, unsigned int a1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
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

static inline int k_win_create(int w, int h, const char *title) {
    unsigned int packed = ((unsigned int)w << 16) | ((unsigned int)h & 0xFFFFu);
    return sc2(14, packed, (unsigned int)title);
}

static inline int k_win_write(int wid, const unsigned char *data, unsigned int len) {
    return sc3(16, (unsigned int)wid, (unsigned int)data, len);
}

static inline int k_win_getkey(int wid) {
    return sc1(18, (unsigned int)wid);
}

static inline int k_detach(void) { return sc0(42); }
static inline int k_sleep_ms(unsigned int ms) { return sc1(27, ms); }
static inline unsigned int k_get_ticks(void) { return (unsigned int)sc0(45); }
static inline int k_debug_exit(unsigned int code) { return sc1(52, code); }
static inline void k_exit(int code) {
    (void)sc1(2, (unsigned int)code);
    for (;;) { __asm__ volatile("hlt"); }
}
static inline int k_write(const char *s, unsigned int n) { return sc3(1, 1, (unsigned int)s, n); }

static void k_write_num(int v) {
    char t[16];
    int i = 0;
    if (v < 0) {
        k_write("-", 1);
        v = -v;
    }
    if (v == 0) {
        k_write("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(t)) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char c = t[--i];
        k_write(&c, 1);
    }
}

static void q_push(int k) {
    int n = (g_qhead + 1) % KEYQ_CAP;
    if (n == g_qtail) return;
    g_keyq[g_qhead] = k;
    g_qhead = n;
}

static int q_pop(void) {
    if (g_qtail == g_qhead) return 0;
    int k = g_keyq[g_qtail];
    g_qtail = (g_qtail + 1) % KEYQ_CAP;
    return k;
}

static int map_key(int k) {
    if (k == KEY_LEFT) return KEY_LEFTARROW;
    if (k == KEY_RIGHT) return KEY_RIGHTARROW;
    if (k == KEY_UP) return KEY_UPARROW;
    if (k == KEY_DOWN) return KEY_DOWNARROW;
    if (k == '\n' || k == '\r') return KEY_ENTER;
    if (k == 27) return KEY_ESCAPE;
    if (k == '\b' || k == 127) return KEY_BACKSPACE;

    if (k >= 'A' && k <= 'Z') k = k - 'A' + 'a';
    if (k >= 32 && k <= 126) return k;
    return 0;
}

void DG_DebugMark(int stage) {
    (void)stage;
}

void DG_Init(void) {
    g_wid = k_win_create(DOOMGENERIC_RESX, DOOMGENERIC_RESY, "DOOM");
    if (g_wid < 0) {
        g_headless = 1;
        k_write("[doom] headless mode (no WM)\n", 27);
    }

    DG_ScreenBuffer = g_fb8;
    memset(g_fb8, 0, sizeof(g_fb8));
}

void DG_DrawFrame(void) {
    int pixels = DOOMGENERIC_RESX * DOOMGENERIC_RESY;
    static unsigned int dbg = 0;
    static int detached = 0;
    dbg++;

    if (g_headless) {
        if (dbg <= 5u || (dbg % 50u) == 0u) {
            k_write("[doom] headless frame=", 20);
            k_write_num((int)dbg);
            k_write("\n", 1);
        }
        // In headless mode, exit after proving startup progressed into frame loop
        // so qemu smoke tests can finish and logs can be captured.
        if (dbg >= 10u) {
            (void)k_write("[doom] headless startup OK\n", 25);
            (void)k_debug_exit(0x21);
            k_exit(0);
        }
        return;
    }

    // Detach from parent on first frame (after all init is complete)
    if (!detached) {
        (void)k_detach();
        detached = 1;
    }

    int wr = k_win_write(g_wid, g_fb8, (unsigned int)pixels);
    if (dbg <= 5u || (dbg % 200u) == 0u) {
        k_write("[doom] frame=", 13);
        k_write_num((int)dbg);
        k_write(" wr=", 4);
        k_write_num(wr);
        k_write("\n", 1);
    }
}

void DG_SleepMs(uint32_t ms) {
    k_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return k_get_ticks() * 10u;  // kernel timer is 100Hz
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (g_headless) return 0;
    int wk;
    while ((wk = k_win_getkey(g_wid)) > 0) {
        int mk = map_key(wk);
        if (mk) q_push(mk);
    }

    int k = q_pop();
    if (!k) return 0;

    *pressed = 1;  // mateOS currently exposes press events only
    *key = (unsigned char)k;
    return 1;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}
