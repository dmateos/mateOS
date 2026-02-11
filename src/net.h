#ifndef _NET_H
#define _NET_H

#include "lib.h"

void net_init(void);
int net_ping(uint32_t ip_be, uint32_t timeout_ms);
void net_set_config(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be);
void net_get_config(uint32_t *ip_be, uint32_t *mask_be, uint32_t *gw_be);

#endif
