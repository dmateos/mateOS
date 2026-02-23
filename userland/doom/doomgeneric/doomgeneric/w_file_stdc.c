//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	WAD I/O functions.
//

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"


typedef struct
{
    wad_file_t wad;
    int fd;
} stdc_wad_file_t;

extern wad_file_class_t stdc_wad_file;

#define SYS_OPEN  36
#define SYS_FREAD 37
#define SYS_CLOSE 39
#define SYS_SEEK  40
#define O_RDONLY  0
#define SEEK_SET  0

static inline int sc1(unsigned int n, unsigned int a1)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static inline int sc2(unsigned int n, unsigned int a1, unsigned int a2)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline int sc3(unsigned int n, unsigned int a1, unsigned int a2, unsigned int a3)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static inline int k_open(const char *path)
{
    return sc2(SYS_OPEN, (unsigned int)path, O_RDONLY);
}

static inline int k_close(int fd)
{
    return sc1(SYS_CLOSE, (unsigned int)fd);
}

static inline int k_seek(int fd, int off, int whence)
{
    return sc3(SYS_SEEK, (unsigned int)fd, (unsigned int)off, (unsigned int)whence);
}

static inline int k_read(int fd, void *buf, unsigned int len)
{
    return sc3(SYS_FREAD, (unsigned int)fd, (unsigned int)buf, len);
}

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    int fd;

    fd = k_open(path);

    if (fd < 0)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    // mateOS bring-up: avoid SEEK_END/ftell probe here, which can stall
    // on large initrd-backed files with the current stdio shim.
    // For WAD files this length is not required during normal open/read path.
    result->wad.length = 0;
    result->fd = fd;

    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *) wad;

    k_close(stdc_wad->fd);
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    int result;

    stdc_wad = (stdc_wad_file_t *) wad;

    // Jump to the specified position in the file.
    if (k_seek(stdc_wad->fd, (int)offset, SEEK_SET) < 0)
    {
        return 0;
    }

    // Read into the buffer.
    result = k_read(stdc_wad->fd, buffer, (unsigned int)buffer_len);
    if (result <= 0)
    {
        return 0;
    }
    return (size_t) result;
}


wad_file_class_t stdc_wad_file = 
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};
