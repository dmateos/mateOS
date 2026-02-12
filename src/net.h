#ifndef _NET_H
#define _NET_H

#include "lib.h"

void net_init(void);
void net_poll(void);
int net_ping(uint32_t ip_be, uint32_t timeout_ms);
void net_set_config(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be);
void net_get_config(uint32_t *ip_be, uint32_t *mask_be, uint32_t *gw_be);

// TCP socket API (kernel-side, called from syscall handler)
int net_sock_listen(uint16_t port);
int net_sock_accept(int fd);
int net_sock_send(int fd, const void *buf, uint32_t len);
int net_sock_recv(int fd, void *buf, uint32_t len);
int net_sock_close(int fd);

#endif
