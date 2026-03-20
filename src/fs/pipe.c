#include "pipe.h"
#include "vfs.h"
#include "proc/task.h"
#include "utils/kring.h"

// ---- slot table -------------------------------------------------------

typedef struct {
    int      in_use;
    char     name[PIPE_NAME_MAX];
    uint8_t  buf_storage[PIPE_BUF_SIZE];
    kring_u8_t ring;
} pipe_slot_t;

static pipe_slot_t pipes[PIPE_MAX];

// Per-open-fd record: which pipe slot + direction (read/write)

typedef struct {
    int in_use;
    int pipe_idx;  // index into pipes[]
    int writable;  // 1 = writer fd, 0 = reader fd
} pipe_fd_t;

static pipe_fd_t pipe_fds[PIPE_FD_MAX];

// ---- helpers -----------------------------------------------------------

// Strip "/pipe/" prefix and return bare name pointer.
// Accepts "/pipe/foo", "pipe/foo", "/pipe" (returns ""), etc.
static const char *pipe_bare_name(const char *path) {
    if (!path) return NULL;
    // skip leading slash
    if (path[0] == '/') path++;
    // expect "pipe"
    if (path[0]=='p' && path[1]=='i' && path[2]=='p' && path[3]=='e') {
        path += 4;
        if (path[0] == '/') path++;
        return path;  // may be empty string for the dir itself
    }
    return NULL;
}

static int pipe_find_by_name(const char *name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < PIPE_MAX; i++) {
        if (pipes[i].in_use && strcmp(pipes[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int pipe_alloc_fd(int pipe_idx, int writable) {
    for (int i = 0; i < PIPE_FD_MAX; i++) {
        if (!pipe_fds[i].in_use) {
            pipe_fds[i].in_use    = 1;
            pipe_fds[i].pipe_idx  = pipe_idx;
            pipe_fds[i].writable  = writable;
            return i;
        }
    }
    return -1;
}

// ---- public API --------------------------------------------------------

void pipe_init(void) {
    for (int i = 0; i < PIPE_MAX; i++) {
        pipes[i].in_use = 0;
        kring_u8_init(&pipes[i].ring, pipes[i].buf_storage, PIPE_BUF_SIZE);
    }
    for (int i = 0; i < PIPE_FD_MAX; i++)
        pipe_fds[i].in_use = 0;
}

int pipe_create(const char *name) {
    if (!name || !name[0]) return -1;
    // duplicate check
    if (pipe_find_by_name(name) >= 0) return -1;
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].in_use) {
            pipes[i].in_use = 1;
            // safe copy
            int j = 0;
            for (; j < PIPE_NAME_MAX - 1 && name[j]; j++)
                pipes[i].name[j] = name[j];
            pipes[i].name[j] = '\0';
            kring_u8_reset(&pipes[i].ring);
            kprintf("[pipe] created '%s'\n", pipes[i].name);
            return 0;
        }
    }
    return -1;  // no free slot
}

int pipe_destroy(const char *name) {
    int idx = pipe_find_by_name(name);
    if (idx < 0) return -1;
    pipes[idx].in_use = 0;
    pipes[idx].name[0] = '\0';
    kring_u8_reset(&pipes[idx].ring);
    // Any tasks blocked on this pipe will naturally unblock on next yield
    // because pipe_read/pipe_write check in_use inside their loop.
    kprintf("[pipe] destroyed '%s'\n", name);
    return 0;
}

int pipe_open(const char *path, int flags) {
    const char *name = pipe_bare_name(path);
    if (!name || !name[0]) return -1;

    int idx = pipe_find_by_name(name);
    if (idx < 0) return -1;

    int access = flags & 0x3;
    if (access != O_RDONLY && access != O_WRONLY) return -1;

    int fd_slot = pipe_alloc_fd(idx, access == O_WRONLY);
    return fd_slot;
}

int pipe_read(int handle, void *buf, uint32_t len) {
    if (handle < 0 || handle >= PIPE_FD_MAX) return -1;
    if (!pipe_fds[handle].in_use) return -1;
    if (pipe_fds[handle].writable) return -1;  // write-only fd

    int idx = pipe_fds[handle].pipe_idx;
    uint8_t *out = (uint8_t *)buf;
    uint32_t got = 0;

    while (got < len) {
        // Check pipe still exists
        if (!pipes[idx].in_use) return got > 0 ? (int)got : -1;

        uint8_t byte;
        if (kring_u8_pop(&pipes[idx].ring, &byte) == 0) {
            out[got++] = byte;
        } else {
            // Buffer empty — if we already have some bytes, return them
            if (got > 0) break;
            // Otherwise block until a writer pushes data
            task_yield();
        }
    }
    return (int)got;
}

int pipe_write(int handle, const void *buf, uint32_t len) {
    if (handle < 0 || handle >= PIPE_FD_MAX) return -1;
    if (!pipe_fds[handle].in_use) return -1;
    if (!pipe_fds[handle].writable) return -1;  // read-only fd

    int idx = pipe_fds[handle].pipe_idx;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t sent = 0;

    while (sent < len) {
        if (!pipes[idx].in_use) return sent > 0 ? (int)sent : -1;

        if (kring_u8_push(&pipes[idx].ring, in[sent]) == 0) {
            sent++;
        } else {
            // Buffer full — block (yield) until reader drains some space
            if (sent > 0) break;
            task_yield();
        }
    }
    return (int)sent;
}

int pipe_close(int handle) {
    if (handle < 0 || handle >= PIPE_FD_MAX) return -1;
    if (!pipe_fds[handle].in_use) return -1;
    pipe_fds[handle].in_use = 0;
    return 0;
}

int pipe_stat(const char *path, void *st_out) {
    vfs_stat_t *st = (vfs_stat_t *)st_out;
    if (!path || !st) return -1;

    const char *name = pipe_bare_name(path);
    if (!name) return -1;

    if (!name[0]) {
        // stat of /pipe directory itself
        st->size = 0;
        st->type = VFS_DIR;
        return 0;
    }

    int idx = pipe_find_by_name(name);
    if (idx < 0) return -1;
    st->size = kring_u8_used(&pipes[idx].ring);
    st->type = VFS_FILE;
    return 0;
}

int pipe_readdir(int index, char *buf, uint32_t size) {
    if (!buf || size == 0 || index < 0) return 0;
    int found = 0;
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].in_use) continue;
        if (found == index) {
            uint32_t n = 0;
            while (n < size - 1 && pipes[i].name[n])
                n++;
            for (uint32_t j = 0; j < n; j++)
                buf[j] = pipes[i].name[j];
            buf[n] = '\0';
            return (int)(n + 1);
        }
        found++;
    }
    return 0;
}

int pipe_is_pipe_path(const char *path) {
    if (!path) return 0;
    const char *p = path;
    if (p[0] == '/') p++;
    return (p[0]=='p' && p[1]=='i' && p[2]=='p' && p[3]=='e' &&
            (p[4]=='\0' || p[4]=='/'));
}
