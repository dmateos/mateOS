#include "window.h"
#include "liballoc/liballoc_1_1.h"

static kernel_window_t windows[MAX_WINDOWS];

void window_init(void) {
    memset(windows, 0, sizeof(windows));
}

int window_create(uint32_t pid, int w, int h, const char *title) {
    if (w <= 0 || w > WIN_MAX_WIDTH || h <= 0 || h > WIN_MAX_HEIGHT)
        return -1;

    // Find free slot
    int wid = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) { wid = i; break; }
    }
    if (wid < 0) return -1;

    uint32_t buf_size = (uint32_t)(w * h);
    uint8_t *buf = (uint8_t *)kmalloc(buf_size);
    if (!buf) return -1;

    memset(buf, 0, buf_size);

    kernel_window_t *win = &windows[wid];
    win->active = 1;
    win->owner_pid = pid;
    win->w = w;
    win->h = h;
    win->buffer = buf;
    win->buf_size = buf_size;
    win->key_head = 0;
    win->key_tail = 0;

    if (title) {
        int i;
        for (i = 0; i < WIN_TITLE_MAX - 1 && title[i]; i++)
            win->title[i] = title[i];
        win->title[i] = '\0';
    } else {
        win->title[0] = '\0';
    }

    return wid;
}

int window_destroy(int wid, uint32_t pid) {
    if (wid < 0 || wid >= MAX_WINDOWS) return -1;
    kernel_window_t *win = &windows[wid];
    if (!win->active || win->owner_pid != pid) return -1;

    if (win->buffer) kfree(win->buffer);
    memset(win, 0, sizeof(kernel_window_t));
    return 0;
}

int window_write(int wid, uint32_t pid, const uint8_t *data, uint32_t len) {
    if (wid < 0 || wid >= MAX_WINDOWS) return -1;
    kernel_window_t *win = &windows[wid];
    if (!win->active || win->owner_pid != pid) return -1;
    if (!data || len == 0) return -1;

    uint32_t to_copy = (len < win->buf_size) ? len : win->buf_size;
    memcpy(win->buffer, data, to_copy);
    return (int)to_copy;
}

int window_read(int wid, uint8_t *dest, uint32_t len) {
    if (wid < 0 || wid >= MAX_WINDOWS) return -1;
    kernel_window_t *win = &windows[wid];
    if (!win->active || !dest || len == 0) return -1;

    uint32_t to_copy = (len < win->buf_size) ? len : win->buf_size;
    memcpy(dest, win->buffer, to_copy);
    return (int)to_copy;
}

int window_getkey(int wid, uint32_t pid) {
    if (wid < 0 || wid >= MAX_WINDOWS) return -1;
    kernel_window_t *win = &windows[wid];
    if (!win->active || win->owner_pid != pid) return -1;

    if (win->key_head == win->key_tail) return 0;  // Empty
    uint8_t key = win->key_buf[win->key_tail];
    win->key_tail = (win->key_tail + 1) % WIN_KEY_BUF_SIZE;
    return (int)key;
}

int window_sendkey(int wid, uint8_t key) {
    if (wid < 0 || wid >= MAX_WINDOWS) return -1;
    kernel_window_t *win = &windows[wid];
    if (!win->active) return -1;

    int next = (win->key_head + 1) % WIN_KEY_BUF_SIZE;
    if (next == win->key_tail) return -1;  // Full
    win->key_buf[win->key_head] = key;
    win->key_head = next;
    return 0;
}

int window_list(win_info_t *out, int max_count) {
    if (!out || max_count <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_WINDOWS && count < max_count; i++) {
        if (windows[i].active) {
            out[count].window_id = i;
            out[count].owner_pid = windows[i].owner_pid;
            out[count].w = windows[i].w;
            out[count].h = windows[i].h;
            memcpy(out[count].title, windows[i].title, WIN_TITLE_MAX);
            count++;
        }
    }
    return count;
}

void window_cleanup_pid(uint32_t pid) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].owner_pid == pid) {
            if (windows[i].buffer) kfree(windows[i].buffer);
            memset(&windows[i], 0, sizeof(kernel_window_t));
        }
    }
}
