#ifndef MATEOS_UNISTD_H
#define MATEOS_UNISTD_H

#include <stddef.h>

int close(int fd);
int read(int fd, void *buf, unsigned int len);
int write(int fd, const void *buf, unsigned int len);
int lseek(int fd, int offset, int whence);
int unlink(const char *path);
int getpid(void);
char *getcwd(char *buf, size_t size);
int execvp(const char *file, char *const argv[]);
int access(const char *path, int mode);
long sysconf(int name);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#define _SC_PAGESIZE 30

#endif
