#ifndef _WINDOW_H
#define _WINDOW_H

#include "lib.h"

#define MAX_WINDOWS 8
#define WIN_TITLE_MAX 32
#define WIN_KEY_BUF_SIZE 16
#define WIN_MAX_WIDTH  600
#define WIN_MAX_HEIGHT 500

typedef struct {
    int active;
    uint32_t owner_pid;
    int w, h;
    char title[WIN_TITLE_MAX];
    uint8_t *buffer;              // kmalloc'd pixel buffer (w*h bytes)
    uint32_t buf_size;
    uint8_t key_buf[WIN_KEY_BUF_SIZE];
    int key_head, key_tail;
} kernel_window_t;

// Returned to userland by win_list
typedef struct {
    int window_id;
    uint32_t owner_pid;
    int w, h;
    char title[WIN_TITLE_MAX];
} win_info_t;

void window_init(void);
int window_create(uint32_t pid, int w, int h, const char *title);
int window_destroy(int wid, uint32_t pid);
int window_write(int wid, uint32_t pid, const uint8_t *data, uint32_t len);
int window_read(int wid, uint8_t *dest, uint32_t len);
int window_getkey(int wid, uint32_t pid);
int window_sendkey(int wid, uint8_t key);
int window_list(win_info_t *out, int max_count);
void window_cleanup_pid(uint32_t pid);

#endif
