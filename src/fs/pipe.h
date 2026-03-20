#ifndef _PIPE_H
#define _PIPE_H

#include "lib.h"

// Named kernel pipes accessible at /pipe/<name>.
// A pipe has a fixed 512-byte ring buffer. Writers block if the buffer is
// full; readers block if the buffer is empty.
// Pipes persist until explicitly destroyed — they outlive individual FDs.

#define PIPE_MAX        8     // max simultaneously open named pipes
#define PIPE_BUF_SIZE   512   // ring buffer capacity per pipe
#define PIPE_NAME_MAX   32    // max name length (not including /pipe/ prefix)
#define PIPE_FD_MAX     (PIPE_MAX * 4)  // max open pipe file descriptors

// Initialize pipe subsystem (called from vfs_init)
void pipe_init(void);

// Create a named pipe. Returns 0 on success, -1 if name is taken or at limit.
// name should be the bare name (e.g. "kbd"), not the full "/pipe/kbd" path.
int pipe_create(const char *name);

// Destroy a named pipe by name. Wakes any blocked readers/writers with error.
// Returns 0 on success, -1 if not found.
int pipe_destroy(const char *name);

// Open a pipe fd slot. flags must be O_RDONLY or O_WRONLY.
// Returns a non-negative slot handle (for vfs_fd_table fs_handle), or -1.
int pipe_open(const char *path, int flags);

// Read up to len bytes. Blocks if empty (yields until data arrives).
// Returns bytes read, or -1 on error (pipe destroyed while blocking).
int pipe_read(int handle, void *buf, uint32_t len);

// Write up to len bytes. Blocks if buffer full.
// Returns bytes written, or -1 on error.
int pipe_write(int handle, const void *buf, uint32_t len);

// Close a pipe fd slot (does not destroy the pipe).
int pipe_close(int handle);

// Stat a /pipe path. Returns 0 on success (fills size=used bytes, type=VFS_FILE).
// Path may be "/pipe" (dir) or "/pipe/<name>" (file).
int pipe_stat(const char *path, void *st_out);

// List pipe names for readdir of /pipe.
// Returns bytes written to buf (name + NUL), or 0 if index is out of range.
int pipe_readdir(int index, char *buf, uint32_t size);

// Check if a path is under /pipe (prefix match "/pipe" or "/pipe/")
int pipe_is_pipe_path(const char *path);

#endif
