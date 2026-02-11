#ifndef _NET_H
#define _NET_H

#include "lib.h"

void net_init(void);
int net_ping(uint32_t ip_be, uint32_t timeout_ms);

#endif
