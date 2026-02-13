#include "window.h"
#include "liballoc/liballoc_1_1.h"
#include "arch/i686/cpu.h"

static kernel_window_t windows[MAX_WINDOWS];

// Decode a generation-encoded window ID and validate
static kernel_window_t *win_get(int wid) {
    int slot = WIN_SLOT(wid);
    if (slot < 0 || slot >= MAX_WINDOWS) return NULL;
    kernel_window_t *win = &windows[slot];
    if (!win->active) return NULL;
    if (win->generation != WIN_GEN(wid)) return NULL;
    return win;
}

void window_init(void) {
    memset(windows, 0, sizeof(windows));
}

int window_create(uint32_t pid, int w, int h, const char *title) {
    if (w <= 0 || w > WIN_MAX_WIDTH || h <= 0 || h > WIN_MAX_HEIGHT)
        return -1;

    unsigned int flags = cpu_irq_save();

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) { slot = i; break; }
    }
    if (slot < 0) { cpu_irq_restore(flags); return -1; }

    uint32_t buf_size = (uint32_t)(w * h);
    uint8_t *buf = (uint8_t *)kmalloc(buf_size);
    if (!buf) { cpu_irq_restore(flags); return -1; }

    memset(buf, 0, buf_size);

    kernel_window_t *win = &windows[slot];
    win->generation++;
    win->active = 1;
    win->owner_pid = pid;
    win->w = w;
    win->h = h;
    win->buffer = buf;
    win->buf_size = buf_size;
    win->key_head = 0;
    win->key_tail = 0;
    win->text_head = 0;
    win->text_tail = 0;

    if (title) {
        int i;
        for (i = 0; i < WIN_TITLE_MAX - 1 && title[i]; i++)
            win->title[i] = title[i];
        win->title[i] = '\0';
    } else {
        win->title[0] = '\0';
    }

    int wid = WIN_MAKE_ID(slot, win->generation);
    cpu_irq_restore(flags);
    return wid;
}

int window_destroy(int wid, uint32_t pid) {
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win || win->owner_pid != pid) { cpu_irq_restore(flags); return -1; }

    uint16_t gen = win->generation;
    if (win->buffer) kfree(win->buffer);
    memset(win, 0, sizeof(kernel_window_t));
    win->generation = gen;  // Preserve generation for next reuse
    cpu_irq_restore(flags);
    return 0;
}

int window_write(int wid, uint32_t pid, const uint8_t *data, uint32_t len) {
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win || win->owner_pid != pid) { cpu_irq_restore(flags); return -1; }
    if (!data || len == 0) { cpu_irq_restore(flags); return -1; }

    uint32_t to_copy = (len < win->buf_size) ? len : win->buf_size;
    memcpy(win->buffer, data, to_copy);
    cpu_irq_restore(flags);
    return (int)to_copy;
}

int window_read(int wid, uint8_t *dest, uint32_t len) {
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win || !dest || len == 0) { cpu_irq_restore(flags); return -1; }

    uint32_t to_copy = (len < win->buf_size) ? len : win->buf_size;
    memcpy(dest, win->buffer, to_copy);
    cpu_irq_restore(flags);
    return (int)to_copy;
}

int window_getkey(int wid, uint32_t pid) {
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win || win->owner_pid != pid) { cpu_irq_restore(flags); return -1; }

    if (win->key_head == win->key_tail) { cpu_irq_restore(flags); return 0; }
    uint8_t key = win->key_buf[win->key_tail];
    win->key_tail = (win->key_tail + 1) % WIN_KEY_BUF_SIZE;
    cpu_irq_restore(flags);
    return (int)key;
}

int window_sendkey(int wid, uint8_t key) {
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win) { cpu_irq_restore(flags); return -1; }

    int next = (win->key_head + 1) % WIN_KEY_BUF_SIZE;
    if (next == win->key_tail) { cpu_irq_restore(flags); return -1; }
    win->key_buf[win->key_head] = key;
    win->key_head = next;
    cpu_irq_restore(flags);
    return 0;
}

int window_list(win_info_t *out, int max_count) {
    if (!out || max_count <= 0) return 0;
    unsigned int flags = cpu_irq_save();
    int count = 0;
    for (int i = 0; i < MAX_WINDOWS && count < max_count; i++) {
        if (windows[i].active) {
            out[count].window_id = WIN_MAKE_ID(i, windows[i].generation);
            out[count].owner_pid = windows[i].owner_pid;
            out[count].w = windows[i].w;
            out[count].h = windows[i].h;
            memcpy(out[count].title, windows[i].title, WIN_TITLE_MAX);
            count++;
        }
    }
    cpu_irq_restore(flags);
    return count;
}

void window_cleanup_pid(uint32_t pid) {
    unsigned int flags = cpu_irq_save();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].owner_pid == pid) {
            uint16_t gen = windows[i].generation;
            if (windows[i].buffer) kfree(windows[i].buffer);
            memset(&windows[i], 0, sizeof(kernel_window_t));
            windows[i].generation = gen;  // Preserve for next reuse
        }
    }
    cpu_irq_restore(flags);
}

int window_append_text(int wid, const char *data, int len) {
    if (!data || len <= 0) return -1;
    unsigned int flags = cpu_irq_save();
    // Use slot directly — children writing here don't have the generation-encoded wid,
    // they have the raw wid from stdout_wid which IS generation-encoded
    kernel_window_t *win = win_get(wid);
    if (!win) { cpu_irq_restore(flags); return -1; }

    int written = 0;
    for (int i = 0; i < len; i++) {
        int next = (win->text_head + 1) % WIN_TEXT_BUF_SIZE;
        if (next == win->text_tail) break;  // Full, drop excess
        win->text_buf[win->text_head] = data[i];
        win->text_head = next;
        written++;
    }
    cpu_irq_restore(flags);
    return written;
}

int window_read_text(int wid, uint32_t pid, char *dest, int max_len) {
    if (!dest || max_len <= 0) return 0;
    unsigned int flags = cpu_irq_save();
    kernel_window_t *win = win_get(wid);
    if (!win || win->owner_pid != pid) { cpu_irq_restore(flags); return -1; }

    int count = 0;
    while (count < max_len && win->text_tail != win->text_head) {
        dest[count++] = win->text_buf[win->text_tail];
        win->text_tail = (win->text_tail + 1) % WIN_TEXT_BUF_SIZE;
    }
    cpu_irq_restore(flags);
    return count;
}
