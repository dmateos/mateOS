#ifndef MATEOS_FCNTL_H
#define MATEOS_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  4
#define O_TRUNC  8
#define O_BINARY 0

#ifndef _USERLAND_SYSCALLS_H
int open(const char *path, int flags, ...);
#endif

#endif
