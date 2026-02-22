#ifndef MATEOS_INTTYPES_H
#define MATEOS_INTTYPES_H

#include <stdint.h>

#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"

typedef struct {
    int quot;
    int rem;
} div_t;

#endif
