#ifndef _DOOM_SYS_STAT_H
#define _DOOM_SYS_STAT_H

struct stat {
    unsigned int st_size;
    unsigned int st_mode;
};

int mkdir(const char *path, int mode);

#endif
