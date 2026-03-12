// Conway's Game of Life for mateOS WM
// Space: pause/resume  R: randomize  C: clear  Q/ESC: quit
// Left-click: toggle cell

#include "libc.h"
#include "syscalls.h"
#include "ugfx.h"

#define W 500
#define H 350

// Status bar at bottom
#define STATUS_H 12
#define GRID_H   (H - STATUS_H)

// Cell size in pixels
#define CELL  5

#define COLS  (W / CELL)        // 100
#define ROWS  (GRID_H / CELL)   // 67

// Double-buffer the grid to avoid in-place update artefacts
static unsigned char grid[ROWS][COLS];
static unsigned char next[ROWS][COLS];
static unsigned char fbuf[W * H];  // pixel framebuffer

static int wid      = -1;
static int paused   = 0;
static int gen      = 0;
static int alive    = 0;

// palette indices
#define COL_DEAD   15   // white
#define COL_ALIVE  22   // dark green
#define COL_GRID   248  // very light grey grid lines
#define COL_BAR    236
#define COL_TXT    252

// ---- Grid helpers ----

static void grid_clear(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = 0;
    gen = 0; alive = 0;
}

static void grid_randomize(void) {
    // Simple LCG seeded from tick counter
    unsigned int rng = get_ticks() ^ 0xDEADBEEFu;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            rng = rng * 1664525u + 1013904223u;
            grid[r][c] = (rng >> 23) & 1; // ~25% alive
        }
    }
    gen = 0;
}

static int count_alive(void) {
    int n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            n += grid[r][c];
    return n;
}

// One Game of Life step into next[], then swap
static void grid_step(void) {
    for (int r = 0; r < ROWS; r++) {
        int rp = (r + ROWS - 1) % ROWS;
        int rn = (r + 1) % ROWS;
        for (int c = 0; c < COLS; c++) {
            int cp = (c + COLS - 1) % COLS;
            int cn = (c + 1) % COLS;
            int n = grid[rp][cp] + grid[rp][c] + grid[rp][cn]
                  + grid[r ][cp]               + grid[r ][cn]
                  + grid[rn][cp] + grid[rn][c] + grid[rn][cn];
            unsigned char cur = grid[r][c];
            // Born if dead with exactly 3 neighbours
            // Survives if alive with 2 or 3 neighbours
            next[r][c] = (!cur && n == 3) || (cur && (n == 2 || n == 3));
        }
    }
    // Swap
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = next[r][c];
    gen++;
}

// ---- Drawing ----

static void draw_status(void) {
    ugfx_buf_rect(fbuf, W, H, 0, GRID_H, W, STATUS_H, COL_BAR);
    ugfx_buf_hline(fbuf, W, H, 0, GRID_H, W, 240);

    // "Gen: NNNN  Alive: NNNN  [PAUSED]"
    char s[64];
    int p = 0;

    // "Gen: "
    const char *gs = "Gen: ";
    for (int i = 0; gs[i]; i++) s[p++] = gs[i];
    // append gen number
    char tmp[12];
    itoa(gen, tmp);
    for (int i = 0; tmp[i]; i++) s[p++] = tmp[i];

    const char *as = "  Alive: ";
    for (int i = 0; as[i]; i++) s[p++] = as[i];
    itoa(alive, tmp);
    for (int i = 0; tmp[i]; i++) s[p++] = tmp[i];

    if (paused) {
        const char *ps = "  [PAUSED]";
        for (int i = 0; ps[i]; i++) s[p++] = ps[i];
    } else {
        const char *rs = "  SPC pause  R rand  C clear  Q quit";
        for (int i = 0; rs[i]; i++) s[p++] = rs[i];
    }
    s[p] = '\0';

    ugfx_buf_string(fbuf, W, H, 4, GRID_H + 2, s, COL_TXT);
}

static void redraw(void) {
    // Draw grid cells
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            unsigned char col = grid[r][c] ? COL_ALIVE : COL_DEAD;
            ugfx_buf_rect(fbuf, W, H,
                          c * CELL, r * CELL,
                          CELL - 1, CELL - 1, col);
        }
    }
    draw_status();
    win_write(wid, fbuf, sizeof(fbuf));
}

// Toggle cell at pixel coords (from mouse click)
static void toggle_cell(int px, int py) {
    if (px < 0 || py < 0 || px >= W || py >= GRID_H)
        return;
    int c = px / CELL;
    int r = py / CELL;
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
        grid[r][c] ^= 1;
}

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    wid = win_create(W, H, "Game of Life");
    if (wid < 0) {
        print("error: requires window manager\n");
        exit(1);
    }
    detach();

    grid_randomize();
    alive = count_alive();
    redraw();

    int tick = 0;
    int prev_btn = 0;

    while (1) {
        // Handle keyboard
        int key = win_getkey(wid);
        if (key > 0) {
            if (key == 'q' || key == 'Q' || key == 27)
                break;
            if (key == ' ') {
                paused ^= 1;
                draw_status();
                win_write(wid, fbuf, sizeof(fbuf));
            }
            if (key == 'r' || key == 'R') {
                grid_randomize();
                paused = 0;
                alive = count_alive();
                redraw();
            }
            if (key == 'c' || key == 'C') {
                grid_clear();
                paused = 1;
                alive = 0;
                redraw();
            }
        }

        // Handle mouse click to toggle cells
        {
            int mx, my;
            unsigned char btn;
            if (getmouse(&mx, &my, &btn) == 0) {
                // Only toggle on fresh press (edge detect), not held
                if ((btn & 1) && !(prev_btn & 1)) {
                    toggle_cell(mx, my);
                    alive = count_alive();
                    redraw();
                }
                prev_btn = btn;
            }
        }

        // Advance simulation every ~5 yields when running
        if (!paused) {
            tick++;
            if (tick >= 5) {
                tick = 0;
                grid_step();
                alive = count_alive();
                redraw();
            }
        }

        yield();
    }

    win_destroy(wid);
    exit(0);
}
