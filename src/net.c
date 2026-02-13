#include "net.h"
#include "lib.h"
#include "drivers/rtl8139.h"
#include "arch/i686/timer.h"
#include "arch/i686/cpu.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/icmp.h"
#include "lwip/tcp.h"
#include "netif/ethernet.h"

// ---- lwIP netif ----
static struct netif rtl_netif;
static int lwip_ready = 0;

// ---- Feed received frame to lwIP ----
static void net_rx_to_lwip(uint8_t *data, uint16_t len) {
  if (!lwip_ready) return;
  struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
  if (!p) return;
  memcpy(p->payload, data, len);
  if (rtl_netif.input(p, &rtl_netif) != ERR_OK) {
    pbuf_free(p);
  }
}

// ---- lwIP sys_now() — required for timeouts ----
uint32_t sys_now(void) {
  return get_tick_count() * 10;  // 100Hz -> milliseconds
}

// sys_arch_protect/unprotect defined inline in lwipopts.h

// ---- lwIP netif TX callback ----
static err_t net_linkoutput(struct netif *netif __attribute__((unused)),
                            struct pbuf *p) {
  rtl8139_send((const uint8_t *)p->payload, (uint16_t)p->tot_len);
  return ERR_OK;
}

// ---- lwIP netif init callback ----
static err_t net_netif_init(struct netif *netif) {
  netif->linkoutput = net_linkoutput;
  netif->output = etharp_output;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
  netif->hwaddr_len = 6;
  rtl8139_get_mac(netif->hwaddr);
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  return ERR_OK;
}

// ---- ICMP ping via lwIP raw API ----
static volatile int ping_reply_received = 0;

static uint8_t ping_recv_cb(void *arg __attribute__((unused)),
                            struct raw_pcb *pcb __attribute__((unused)),
                            struct pbuf *p,
                            const ip_addr_t *addr __attribute__((unused))) {
  if (p->len >= 20 + 8) {
    struct icmp_echo_hdr *hdr = (struct icmp_echo_hdr *)((uint8_t *)p->payload + 20);
    if (hdr->type == 0) {  // Echo reply
      ping_reply_received = 1;
      pbuf_free(p);
      return 1;  // consumed
    }
  }
  return 0;  // not consumed
}

// ---- Public API ----

void net_init(void) {
  rtl8139_init(net_rx_to_lwip);
  if (!rtl8139_available()) return;

  lwip_init();

  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, 10, 0, 2, 15);
  IP4_ADDR(&mask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 10, 0, 2, 2);
  netif_add(&rtl_netif, &ip, &mask, &gw, NULL, net_netif_init, ethernet_input);
  netif_set_default(&rtl_netif);
  netif_set_up(&rtl_netif);

  lwip_ready = 1;
  printf("[net] lwIP initialized, ip=10.0.2.15\n");
}

void net_poll(void) {
  if (!lwip_ready) return;
  rtl8139_rx_poll();
  sys_check_timeouts();
}

int net_ping(uint32_t ip_be, uint32_t timeout_ms) {
  if (!lwip_ready) return -1;

  cpu_enable_interrupts();

  // Create raw ICMP PCB
  struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return -1;

  raw_recv(pcb, ping_recv_cb, NULL);
  raw_bind(pcb, IP_ADDR_ANY);

  // Build ICMP echo request
  struct pbuf *p = pbuf_alloc(PBUF_IP, (uint16_t)(sizeof(struct icmp_echo_hdr) + 32), PBUF_RAM);
  if (!p) {
    raw_remove(pcb);
    return -1;
  }

  struct icmp_echo_hdr *hdr = (struct icmp_echo_hdr *)p->payload;
  hdr->type = 8;  // Echo request
  hdr->code = 0;
  hdr->id = lwip_htons(0xBEEF);
  hdr->seqno = lwip_htons(1);

  // Fill payload
  uint8_t *payload = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
  for (int i = 0; i < 32; i++) payload[i] = (uint8_t)i;

  // Compute checksum
  hdr->chksum = 0;
  uint32_t sum = 0;
  uint16_t *ptr = (uint16_t *)p->payload;
  uint16_t total = (uint16_t)(sizeof(struct icmp_echo_hdr) + 32);
  for (uint16_t i = 0; i < total / 2; i++) sum += ptr[i];
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  hdr->chksum = (uint16_t)~sum;

  // Send
  ip_addr_t dst;
  uint8_t a = (uint8_t)(ip_be >> 24);
  uint8_t b = (uint8_t)(ip_be >> 16);
  uint8_t c = (uint8_t)(ip_be >> 8);
  uint8_t d = (uint8_t)(ip_be);
  IP4_ADDR(&dst, a, b, c, d);

  ping_reply_received = 0;
  raw_sendto(pcb, p, &dst);
  pbuf_free(p);

  // Wait for reply
  uint32_t start = get_tick_count();
  uint32_t timeout_ticks = (timeout_ms + 9) / 10;
  while (!ping_reply_received) {
    net_poll();
    if ((get_tick_count() - start) > timeout_ticks) {
      raw_remove(pcb);
      return -1;
    }
    cpu_halt();
  }

  raw_remove(pcb);
  return 0;
}

void net_set_config(uint32_t ip_be, uint32_t mask_be, uint32_t gw_be) {
  if (!lwip_ready) return;
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, (ip_be >> 24) & 0xFF, (ip_be >> 16) & 0xFF,
           (ip_be >> 8) & 0xFF, ip_be & 0xFF);
  IP4_ADDR(&mask, (mask_be >> 24) & 0xFF, (mask_be >> 16) & 0xFF,
           (mask_be >> 8) & 0xFF, mask_be & 0xFF);
  IP4_ADDR(&gw, (gw_be >> 24) & 0xFF, (gw_be >> 16) & 0xFF,
           (gw_be >> 8) & 0xFF, gw_be & 0xFF);
  netif_set_addr(&rtl_netif, &ip, &mask, &gw);
  printf("[net] cfg ip=%d.%d.%d.%d\n",
         (ip_be >> 24) & 0xFF, (ip_be >> 16) & 0xFF,
         (ip_be >> 8) & 0xFF, ip_be & 0xFF);
}

void net_get_config(uint32_t *ip_be, uint32_t *mask_be, uint32_t *gw_be) {
  if (!lwip_ready) {
    if (ip_be) *ip_be = 0;
    if (mask_be) *mask_be = 0;
    if (gw_be) *gw_be = 0;
    return;
  }
  const ip4_addr_t *ip = netif_ip4_addr(&rtl_netif);
  const ip4_addr_t *mask = netif_ip4_netmask(&rtl_netif);
  const ip4_addr_t *gw = netif_ip4_gw(&rtl_netif);
  if (ip_be) {
    uint32_t a = ip4_addr_get_u32(ip);
    *ip_be = ((a & 0xFF) << 24) | (((a >> 8) & 0xFF) << 16) |
             (((a >> 16) & 0xFF) << 8) | ((a >> 24) & 0xFF);
  }
  if (mask_be) {
    uint32_t a = ip4_addr_get_u32(mask);
    *mask_be = ((a & 0xFF) << 24) | (((a >> 8) & 0xFF) << 16) |
               (((a >> 16) & 0xFF) << 8) | ((a >> 24) & 0xFF);
  }
  if (gw_be) {
    uint32_t a = ip4_addr_get_u32(gw);
    *gw_be = ((a & 0xFF) << 24) | (((a >> 8) & 0xFF) << 16) |
             (((a >> 16) & 0xFF) << 8) | ((a >> 24) & 0xFF);
  }
}

// ==== TCP Socket Table ====

#define MAX_SOCKETS    8
#define SOCK_RX_BUF    4096

#define SOCK_UNUSED    0
#define SOCK_LISTEN    1
#define SOCK_STREAM    2

typedef struct {
  int in_use;
  int type;              // SOCK_LISTEN or SOCK_STREAM
  struct tcp_pcb *pcb;
  int accepted_fd;       // For listeners: fd of newly accepted connection (-1 if none)
  uint8_t rx_buf[SOCK_RX_BUF];
  int rx_head;           // Write position
  int rx_tail;           // Read position
  int rx_closed;         // Remote sent FIN
  int err;               // Error flag
} ksocket_t;

static ksocket_t sockets[MAX_SOCKETS];

static int alloc_socket(void) {
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (!sockets[i].in_use) {
      memset(&sockets[i], 0, sizeof(ksocket_t));
      sockets[i].in_use = 1;
      sockets[i].accepted_fd = -1;
      return i;
    }
  }
  return -1;
}

// Ring buffer helpers
static int rx_buf_used(ksocket_t *s) {
  int used = s->rx_head - s->rx_tail;
  if (used < 0) used += SOCK_RX_BUF;
  return used;
}

// lwIP callback: data received on a connected socket
static err_t sock_recv_cb(void *arg, struct tcp_pcb *tpcb,
                          struct pbuf *p, err_t err) {
  ksocket_t *s = (ksocket_t *)arg;
  (void)err;

  if (!p) {
    s->rx_closed = 1;
    return ERR_OK;
  }

  // Copy pbuf chain into rx ring buffer
  struct pbuf *q;
  for (q = p; q != NULL; q = q->next) {
    uint16_t copy_len = q->len;
    uint8_t *src = (uint8_t *)q->payload;
    for (uint16_t i = 0; i < copy_len; i++) {
      int next_head = (s->rx_head + 1) % SOCK_RX_BUF;
      if (next_head == s->rx_tail) break;
      s->rx_buf[s->rx_head] = src[i];
      s->rx_head = next_head;
    }
  }

  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

// lwIP callback: error on a connected socket
static void sock_err_cb(void *arg, err_t err) {
  ksocket_t *s = (ksocket_t *)arg;
  (void)err;
  s->err = 1;
  s->pcb = NULL;
}

// lwIP callback: new connection accepted on listener
static err_t sock_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
  ksocket_t *ls = (ksocket_t *)arg;
  (void)err;

  if (ls->accepted_fd >= 0) {
    tcp_abort(newpcb);
    return ERR_ABRT;
  }

  int fd = alloc_socket();
  if (fd < 0) {
    tcp_abort(newpcb);
    return ERR_ABRT;
  }

  sockets[fd].type = SOCK_STREAM;
  sockets[fd].pcb = newpcb;
  tcp_arg(newpcb, &sockets[fd]);
  tcp_recv(newpcb, sock_recv_cb);
  tcp_err(newpcb, sock_err_cb);

  ls->accepted_fd = fd;
  return ERR_OK;
}

// ---- Public TCP socket API ----

int net_sock_listen(uint16_t port) {
  if (!lwip_ready) return -1;

  int fd = alloc_socket();
  if (fd < 0) return -1;

  struct tcp_pcb *pcb = tcp_new();
  if (!pcb) {
    sockets[fd].in_use = 0;
    return -1;
  }

  err_t e = tcp_bind(pcb, IP_ADDR_ANY, port);
  if (e != ERR_OK) {
    tcp_close(pcb);
    sockets[fd].in_use = 0;
    return -1;
  }

  struct tcp_pcb *lpcb = tcp_listen(pcb);
  if (!lpcb) {
    tcp_close(pcb);
    sockets[fd].in_use = 0;
    return -1;
  }

  sockets[fd].type = SOCK_LISTEN;
  sockets[fd].pcb = lpcb;
  tcp_arg(lpcb, &sockets[fd]);
  tcp_accept(lpcb, sock_accept_cb);

  return fd;
}

int net_sock_accept(int fd) {
  if (fd < 0 || fd >= MAX_SOCKETS) return -1;
  ksocket_t *s = &sockets[fd];
  if (!s->in_use || s->type != SOCK_LISTEN) return -1;

  if (s->accepted_fd >= 0) {
    int newfd = s->accepted_fd;
    s->accepted_fd = -1;
    return newfd;
  }

  return -1;
}

int net_sock_send(int fd, const void *buf, uint32_t len) {
  if (fd < 0 || fd >= MAX_SOCKETS) return -1;
  ksocket_t *s = &sockets[fd];
  if (!s->in_use || s->type != SOCK_STREAM || !s->pcb) return -1;

  uint16_t sndbuf = tcp_sndbuf(s->pcb);
  if (sndbuf == 0) return 0;
  if (len > sndbuf) len = sndbuf;

  err_t e = tcp_write(s->pcb, buf, (uint16_t)len, TCP_WRITE_FLAG_COPY);
  if (e != ERR_OK) return -1;

  tcp_output(s->pcb);
  return (int)len;
}

int net_sock_recv(int fd, void *buf, uint32_t len) {
  if (fd < 0 || fd >= MAX_SOCKETS) return -1;
  ksocket_t *s = &sockets[fd];
  if (!s->in_use || s->type != SOCK_STREAM) return -1;

  int avail = rx_buf_used(s);
  if (avail == 0) {
    if (s->rx_closed || s->err || !s->pcb) return 0;
    return -1;
  }

  if ((uint32_t)avail > len) avail = (int)len;

  uint8_t *dst = (uint8_t *)buf;
  for (int i = 0; i < avail; i++) {
    dst[i] = s->rx_buf[s->rx_tail];
    s->rx_tail = (s->rx_tail + 1) % SOCK_RX_BUF;
  }

  return avail;
}

int net_sock_close(int fd) {
  if (fd < 0 || fd >= MAX_SOCKETS) return -1;
  ksocket_t *s = &sockets[fd];
  if (!s->in_use) return -1;

  if (s->pcb) {
    if (s->type == SOCK_STREAM) {
      tcp_arg(s->pcb, NULL);
      tcp_recv(s->pcb, NULL);
      tcp_err(s->pcb, NULL);
    }
    tcp_close(s->pcb);
    s->pcb = NULL;
  }

  s->in_use = 0;
  return 0;
}
