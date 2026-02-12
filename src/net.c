#include "net.h"
#include "lib.h"
#include "pci.h"
#include "arch/i686/io.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/timer.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/prot/ip.h"
#include "lwip/tcp.h"
#include "netif/ethernet.h"

// ---- RTL8139 constants ----
#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

#define RTL_IDR0     0x00
#define RTL_TSD0     0x10
#define RTL_TSAD0    0x20
#define RTL_RBSTART  0x30
#define RTL_CR       0x37
#define RTL_CAPR     0x38
#define RTL_CBR      0x3A
#define RTL_IMR      0x3C
#define RTL_ISR      0x3E
#define RTL_TCR      0x40
#define RTL_RCR      0x44
#define RTL_CONFIG1  0x52

#define RTL_CR_RESET 0x10
#define RTL_CR_RX_EN 0x08
#define RTL_CR_TX_EN 0x04

#define RTL_INT_ROK  0x01
#define RTL_INT_TOK  0x04

#define RTL_RCR_AAP  0x01
#define RTL_RCR_APM  0x02
#define RTL_RCR_AM   0x04
#define RTL_RCR_AB   0x08
#define RTL_RCR_WRAP 0x80

#define RX_BUF_SIZE (8192 + 16 + 1500)
#define TX_BUF_SIZE 2048
#define TX_DESC_COUNT 4

static uint16_t rtl_io = 0;
static uint8_t rtl_irq = 0;

static uint8_t rtl_mac[6] = {0};
static uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(16)));
static int tx_cur = 0;

// ---- lwIP netif ----
static struct netif rtl_netif;
static int lwip_ready = 0;

// ---- RTL8139 send (raw Ethernet frame) ----
static void rtl_send(const uint8_t *data, uint16_t len) {
  if (!rtl_io || len > TX_BUF_SIZE) return;
  int idx = tx_cur % TX_DESC_COUNT;
  memcpy(tx_buf[idx], data, len);
  outl(rtl_io + RTL_TSAD0 + (idx * 4), (uint32_t)tx_buf[idx]);
  outl(rtl_io + RTL_TSD0 + (idx * 4), len);
  tx_cur = (tx_cur + 1) % TX_DESC_COUNT;
}

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

// ---- RTL8139 RX polling ----
static void rtl_rx_poll(void) {
  if (!rtl_io) return;

  uint16_t capr = inw(rtl_io + RTL_CAPR);
  uint16_t cbr = inw(rtl_io + RTL_CBR);
  uint16_t offset = (uint16_t)(capr + 16);

  while (offset != cbr) {
    uint16_t hdr_off = offset % RX_BUF_SIZE;
    uint16_t status = *(uint16_t *)(rx_buf + hdr_off);
    uint16_t length = *(uint16_t *)(rx_buf + hdr_off + 2);

    if (!(status & 0x01) || length < 4) {
      break;
    }

    uint16_t pkt_len = (uint16_t)(length - 4);
    uint16_t data_off = (uint16_t)(hdr_off + 4);

    if (pkt_len > 1514) {
      pkt_len = 1514;
    }

    static uint8_t pkt[1600];
    if (data_off + pkt_len <= RX_BUF_SIZE) {
      memcpy(pkt, rx_buf + data_off, pkt_len);
    } else {
      uint16_t first = (uint16_t)(RX_BUF_SIZE - data_off);
      memcpy(pkt, rx_buf + data_off, first);
      memcpy(pkt + first, rx_buf, (uint16_t)(pkt_len - first));
    }

    net_rx_to_lwip(pkt, pkt_len);

    offset = (uint16_t)(offset + length + 4);
    offset = (uint16_t)((offset + 3) & ~3);
    outw(rtl_io + RTL_CAPR, (uint16_t)(offset - 16));

    capr = inw(rtl_io + RTL_CAPR);
    cbr = inw(rtl_io + RTL_CBR);
  }
}

// ---- RTL8139 IRQ handler ----
static void rtl_irq_handler(uint32_t irq __attribute__((unused)),
                            uint32_t err __attribute__((unused))) {
  if (!rtl_io) return;
  uint16_t isr = inw(rtl_io + RTL_ISR);
  if (!isr) return;
  outw(rtl_io + RTL_ISR, isr);

  if (isr & RTL_INT_ROK) {
    rtl_rx_poll();
  }
}

// ---- RTL8139 init ----
static void rtl_init(void) {
  pci_device_t *dev = pci_find_device(RTL_VENDOR_ID, RTL_DEVICE_ID);
  if (!dev) {
    printf("[net] RTL8139 not found\n");
    return;
  }

  if (!(dev->bar[0] & 0x01)) {
    printf("[net] RTL8139 BAR0 not IO\n");
    return;
  }

  rtl_io = (uint16_t)(dev->bar[0] & 0xFFFC);
  rtl_irq = dev->irq_line;

  pci_enable_bus_mastering(dev);

  outb(rtl_io + RTL_CONFIG1, 0x00);

  outb(rtl_io + RTL_CR, RTL_CR_RESET);
  while (inb(rtl_io + RTL_CR) & RTL_CR_RESET) {}

  for (int i = 0; i < 6; i++) {
    rtl_mac[i] = inb(rtl_io + RTL_IDR0 + i);
  }

  outl(rtl_io + RTL_RBSTART, (uint32_t)rx_buf);

  outb(rtl_io + RTL_CR, RTL_CR_RX_EN | RTL_CR_TX_EN);

  outl(rtl_io + RTL_RCR, RTL_RCR_AB | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_WRAP);

  outw(rtl_io + RTL_IMR, RTL_INT_ROK | RTL_INT_TOK);

  if (rtl_irq != 0 && rtl_irq != 0xFF) {
    register_interrupt_handler((uint8_t)(0x20 + rtl_irq), rtl_irq_handler);
    pic_unmask_irq(rtl_irq);
  }

  printf("[net] RTL8139 io=0x%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
         rtl_io, rtl_irq,
         rtl_mac[0], rtl_mac[1], rtl_mac[2],
         rtl_mac[3], rtl_mac[4], rtl_mac[5]);
}

// ---- lwIP sys_now() — required for timeouts ----
uint32_t sys_now(void) {
  return get_tick_count() * 10;  // 100Hz → milliseconds
}

// sys_arch_protect/unprotect defined inline in lwipopts.h

// ---- lwIP netif TX callback ----
static err_t rtl_linkoutput(struct netif *netif __attribute__((unused)),
                            struct pbuf *p) {
  rtl_send((const uint8_t *)p->payload, (uint16_t)p->tot_len);
  return ERR_OK;
}

// ---- lwIP netif init callback ----
static err_t rtl_netif_init(struct netif *netif) {
  netif->linkoutput = rtl_linkoutput;
  netif->output = etharp_output;
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
  netif->hwaddr_len = 6;
  memcpy(netif->hwaddr, rtl_mac, 6);
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  return ERR_OK;
}

// ---- ICMP ping via lwIP raw API ----
#include "lwip/prot/icmp.h"

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
  rtl_init();
  if (!rtl_io) return;

  lwip_init();

  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, 10, 0, 2, 15);
  IP4_ADDR(&mask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 10, 0, 2, 2);
  netif_add(&rtl_netif, &ip, &mask, &gw, NULL, rtl_netif_init, ethernet_input);
  netif_set_default(&rtl_netif);
  netif_set_up(&rtl_netif);

  lwip_ready = 1;
  printf("[net] lwIP initialized, ip=10.0.2.15\n");
}

void net_poll(void) {
  if (!lwip_ready) return;
  rtl_rx_poll();
  sys_check_timeouts();
}

int net_ping(uint32_t ip_be, uint32_t timeout_ms) {
  if (!lwip_ready) return -1;

  __asm__ volatile("sti");

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
    __asm__ volatile("hlt");
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
    // lwIP stores in network byte order, we return host byte order (big-endian IP)
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
    // FIN received — remote closed
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
      if (next_head == s->rx_tail) break;  // Buffer full, drop remaining
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
  s->pcb = NULL;  // lwIP already freed the pcb
}

// lwIP callback: new connection accepted on listener
static err_t sock_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
  ksocket_t *ls = (ksocket_t *)arg;
  (void)err;

  if (ls->accepted_fd >= 0) {
    // Still have an unaccepted connection — reject this one
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

  ls->accepted_fd = fd;  // Signal to accept() syscall
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

  return -1;  // No pending connection
}

int net_sock_send(int fd, const void *buf, uint32_t len) {
  if (fd < 0 || fd >= MAX_SOCKETS) return -1;
  ksocket_t *s = &sockets[fd];
  if (!s->in_use || s->type != SOCK_STREAM || !s->pcb) return -1;

  uint16_t sndbuf = tcp_sndbuf(s->pcb);
  if (sndbuf == 0) return 0;  // Send buffer full, try again later
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
    if (s->rx_closed || s->err || !s->pcb) return 0;  // Connection closed
    return -1;  // No data yet
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
