#ifndef MATEOS_SYS_STAT_H
#define MATEOS_SYS_STAT_H

#include "../../syscalls.h"

struct stat {
    unsigned int size;
    unsigned int type;
};

#endif
