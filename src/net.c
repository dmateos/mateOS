#include "net.h"
#include "lib.h"
#include "pci.h"
#include "arch/i686/io.h"
#include "arch/i686/interrupts.h"
#include "arch/i686/timer.h"

// Minimal RTL8139 + ARP/ICMP (ping) stack for QEMU

// ---- RTL8139 constants ----
#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

// Register offsets
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

// Command bits
#define RTL_CR_RESET 0x10
#define RTL_CR_RX_EN 0x08
#define RTL_CR_TX_EN 0x04

// Interrupt status bits
#define RTL_INT_ROK  0x01
#define RTL_INT_TOK  0x04

// RX config bits
#define RTL_RCR_AAP  0x01  // Accept all physical
#define RTL_RCR_APM  0x02  // Accept physical match
#define RTL_RCR_AM   0x04  // Accept multicast
#define RTL_RCR_AB   0x08  // Accept broadcast
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

// ---- Minimal network config ----
static uint8_t net_ip[4] = {10, 0, 2, 15};
static uint8_t net_mask[4] = {255, 255, 255, 0};
static uint8_t net_gw[4] = {10, 0, 2, 2};

// ---- Byte order helpers ----
static inline uint16_t bswap16(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t bswap32(uint32_t v) {
  return (v >> 24) |
         ((v >> 8) & 0x0000FF00) |
         ((v << 8) & 0x00FF0000) |
         (v << 24);
}

static inline uint16_t htons(uint16_t v) { return bswap16(v); }
static inline uint16_t ntohs(uint16_t v) { return bswap16(v); }
static inline uint32_t htonl(uint32_t v) { return bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return bswap32(v); }

// ---- Ethernet / ARP / IP / ICMP ----
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800

typedef struct {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t oper;
  uint8_t sha[6];
  uint8_t spa[4];
  uint8_t tha[6];
  uint8_t tpa[4];
} __attribute__((packed)) arp_hdr_t;

typedef struct {
  uint8_t ver_ihl;
  uint8_t tos;
  uint16_t len;
  uint16_t id;
  uint16_t flags_frag;
  uint8_t ttl;
  uint8_t proto;
  uint16_t csum;
  uint32_t src;
  uint32_t dst;
} __attribute__((packed)) ip_hdr_t;

typedef struct {
  uint8_t type;
  uint8_t code;
  uint16_t csum;
  uint16_t id;
  uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

static uint16_t checksum16(const void *data, uint16_t len) {
  const uint16_t *p = (const uint16_t *)data;
  uint32_t sum = 0;
  while (len > 1) {
    sum += *p++;
    len -= 2;
  }
  if (len) {
    sum += *(const uint8_t *)p;
  }
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return (uint16_t)~sum;
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    if (a[i] != b[i]) return 0;
  }
  return 1;
}

static uint32_t ip4_to_u32(const uint8_t ip[4]) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
         ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

// ---- Simple ARP cache (single entry) ----
static uint8_t arp_ip[4] = {0};
static uint8_t arp_mac[6] = {0};
static int arp_valid = 0;

static volatile uint16_t icmp_wait_id = 0;
static volatile uint16_t icmp_wait_seq = 0;
static volatile int icmp_reply_ok = 0;

static void rtl_send(const uint8_t *data, uint16_t len) {
  if (!rtl_io || len > TX_BUF_SIZE) return;
  int idx = tx_cur % TX_DESC_COUNT;
  memcpy(tx_buf[idx], data, len);
  outl(rtl_io + RTL_TSAD0 + (idx * 4), (uint32_t)tx_buf[idx]);
  outl(rtl_io + RTL_TSD0 + (idx * 4), len);
  tx_cur = (tx_cur + 1) % TX_DESC_COUNT;
}

static void arp_request(const uint8_t target_ip[4]) {
  uint8_t frame[sizeof(eth_hdr_t) + sizeof(arp_hdr_t)];
  eth_hdr_t *re = (eth_hdr_t *)frame;
  arp_hdr_t *ra = (arp_hdr_t *)(frame + sizeof(eth_hdr_t));

  memset(re->dst, 0xFF, 6);
  memcpy(re->src, rtl_mac, 6);
  re->type = htons(ETH_TYPE_ARP);

  ra->htype = htons(1);
  ra->ptype = htons(ETH_TYPE_IP);
  ra->hlen = 6;
  ra->plen = 4;
  ra->oper = htons(1);
  memcpy(ra->sha, rtl_mac, 6);
  memcpy(ra->spa, net_ip, 4);
  memset(ra->tha, 0x00, 6);
  memcpy(ra->tpa, target_ip, 4);

  rtl_send(frame, sizeof(frame));
}

static void arp_reply(const eth_hdr_t *eth, const arp_hdr_t *arp) {
  uint8_t frame[sizeof(eth_hdr_t) + sizeof(arp_hdr_t)];
  eth_hdr_t *re = (eth_hdr_t *)frame;
  arp_hdr_t *ra = (arp_hdr_t *)(frame + sizeof(eth_hdr_t));

  memcpy(re->dst, eth->src, 6);
  memcpy(re->src, rtl_mac, 6);
  re->type = htons(ETH_TYPE_ARP);

  ra->htype = htons(1);
  ra->ptype = htons(ETH_TYPE_IP);
  ra->hlen = 6;
  ra->plen = 4;
  ra->oper = htons(2);
  memcpy(ra->sha, rtl_mac, 6);
  memcpy(ra->spa, net_ip, 4);
  memcpy(ra->tha, arp->sha, 6);
  memcpy(ra->tpa, arp->spa, 4);

  rtl_send(frame, sizeof(frame));
}

static void icmp_echo_reply(const eth_hdr_t *eth, const ip_hdr_t *ip,
                            const icmp_hdr_t *icmp, uint16_t icmp_len) {
  uint16_t ip_hlen = (ip->ver_ihl & 0x0F) * 4;
  uint16_t total_len = sizeof(eth_hdr_t) + ip_hlen + icmp_len;

  uint8_t frame[1514];
  if (total_len > sizeof(frame)) return;

  eth_hdr_t *re = (eth_hdr_t *)frame;
  ip_hdr_t *ri = (ip_hdr_t *)(frame + sizeof(eth_hdr_t));
  icmp_hdr_t *ri_icmp = (icmp_hdr_t *)((uint8_t *)ri + ip_hlen);

  memcpy(re->dst, eth->src, 6);
  memcpy(re->src, rtl_mac, 6);
  re->type = htons(ETH_TYPE_IP);

  memcpy(ri, ip, ip_hlen);
  ri->src = ip->dst;
  ri->dst = ip->src;
  ri->ttl = 64;
  ri->csum = 0;
  ri->csum = checksum16(ri, ip_hlen);

  memcpy(ri_icmp, icmp, icmp_len);
  ri_icmp->type = 0;  // Echo reply
  ri_icmp->csum = 0;
  ri_icmp->csum = checksum16(ri_icmp, icmp_len);

  rtl_send(frame, total_len);
}

static void net_handle_frame(uint8_t *data, uint16_t len) {
  if (len < sizeof(eth_hdr_t)) return;
  eth_hdr_t *eth = (eth_hdr_t *)data;
  uint16_t etype = ntohs(eth->type);

  if (etype == ETH_TYPE_ARP) {
    if (len < sizeof(eth_hdr_t) + sizeof(arp_hdr_t)) return;
    arp_hdr_t *arp = (arp_hdr_t *)(data + sizeof(eth_hdr_t));
    if (ntohs(arp->oper) == 1 && bytes_eq(arp->tpa, net_ip, 4)) {
      arp_reply(eth, arp);
    }
    if (ntohs(arp->oper) == 2 && bytes_eq(arp->tpa, net_ip, 4)) {
      memcpy(arp_ip, arp->spa, 4);
      memcpy(arp_mac, arp->sha, 6);
      arp_valid = 1;
    }
    return;
  }

  if (etype == ETH_TYPE_IP) {
    if (len < sizeof(eth_hdr_t) + sizeof(ip_hdr_t)) return;
    ip_hdr_t *ip = (ip_hdr_t *)(data + sizeof(eth_hdr_t));
    if ((ip->ver_ihl >> 4) != 4) return;
    if (!bytes_eq((const uint8_t *)&ip->dst, net_ip, 4)) return;
    uint16_t ip_hlen = (ip->ver_ihl & 0x0F) * 4;
    if (len < sizeof(eth_hdr_t) + ip_hlen) return;
    if (ip->proto == 1) {  // ICMP
      icmp_hdr_t *icmp = (icmp_hdr_t *)((uint8_t *)ip + ip_hlen);
      uint16_t ip_len = ntohs(ip->len);
      uint16_t icmp_len = (ip_len > ip_hlen) ? (ip_len - ip_hlen) : 0;
      if (icmp_len >= sizeof(icmp_hdr_t)) {
        if (icmp->type == 8) {
          icmp_echo_reply(eth, ip, icmp, icmp_len);
        } else if (icmp->type == 0) {
          uint16_t id = ntohs(icmp->id);
          uint16_t seq = ntohs(icmp->seq);
          if (id == icmp_wait_id && seq == icmp_wait_seq) {
            icmp_reply_ok = 1;
          }
        }
      }
    }
  }
}

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

    net_handle_frame(pkt, pkt_len);

    offset = (uint16_t)(offset + length + 4);
    offset = (uint16_t)((offset + 3) & ~3);
    outw(rtl_io + RTL_CAPR, (uint16_t)(offset - 16));

    capr = inw(rtl_io + RTL_CAPR);
    cbr = inw(rtl_io + RTL_CBR);
  }
}

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

  // Power on / enable
  outb(rtl_io + RTL_CONFIG1, 0x00);

  // Reset
  outb(rtl_io + RTL_CR, RTL_CR_RESET);
  while (inb(rtl_io + RTL_CR) & RTL_CR_RESET) {}

  // Read MAC
  for (int i = 0; i < 6; i++) {
    rtl_mac[i] = inb(rtl_io + RTL_IDR0 + i);
  }

  // RX buffer
  outl(rtl_io + RTL_RBSTART, (uint32_t)rx_buf);

  // Enable RX/TX
  outb(rtl_io + RTL_CR, RTL_CR_RX_EN | RTL_CR_TX_EN);

  // Configure RX
  outl(rtl_io + RTL_RCR, RTL_RCR_AB | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_WRAP);

  // Interrupt mask
  outw(rtl_io + RTL_IMR, RTL_INT_ROK | RTL_INT_TOK);

  // Hook IRQ
  if (rtl_irq != 0 && rtl_irq != 0xFF) {
    register_interrupt_handler((uint8_t)(0x20 + rtl_irq), rtl_irq_handler);
    pic_unmask_irq(rtl_irq);
  }

  printf("[net] RTL8139 io=0x%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
         rtl_io, rtl_irq,
         rtl_mac[0], rtl_mac[1], rtl_mac[2],
         rtl_mac[3], rtl_mac[4], rtl_mac[5]);
}

int net_ping(uint32_t ip_be, uint32_t timeout_ms) {
  __asm__ volatile("sti");
  uint8_t dst_ip[4] = {
    (uint8_t)(ip_be >> 24),
    (uint8_t)(ip_be >> 16),
    (uint8_t)(ip_be >> 8),
    (uint8_t)(ip_be)
  };

  // ARP resolve (single-entry cache)
  if (!(arp_valid && bytes_eq(arp_ip, dst_ip, 4))) {
    arp_valid = 0;
    arp_request(dst_ip);
    uint32_t start = get_tick_count();
    uint32_t timeout_ticks = (timeout_ms + 9) / 10;  // 100Hz
    uint32_t spins = 0;
    while (!arp_valid) {
      rtl_rx_poll();
      if ((get_tick_count() - start) > timeout_ticks) {
        return -1;
      }
      if (++spins > timeout_ms * 10000u) {
        return -1;
      }
      __asm__ volatile("hlt");
    }
  }

  // Build ICMP echo request
  uint8_t frame[1500];
  eth_hdr_t *eth = (eth_hdr_t *)frame;
  ip_hdr_t *ip = (ip_hdr_t *)(frame + sizeof(eth_hdr_t));
  icmp_hdr_t *icmp = (icmp_hdr_t *)((uint8_t *)ip + sizeof(ip_hdr_t));
  uint8_t *payload = (uint8_t *)(icmp + 1);
  const uint16_t payload_len = 32;

  memcpy(eth->dst, arp_mac, 6);
  memcpy(eth->src, rtl_mac, 6);
  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons((uint16_t)(sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + payload_len));
  ip->id = htons(0x1234);
  ip->flags_frag = 0;
  ip->ttl = 64;
  ip->proto = 1;
  ip->csum = 0;
  ip->src = htonl(ip4_to_u32(net_ip));
  ip->dst = htonl(ip_be);
  ip->csum = checksum16(ip, sizeof(ip_hdr_t));

  icmp->type = 8;
  icmp->code = 0;
  icmp->id = htons(0xBEEF);
  icmp->seq = htons(1);
  for (uint16_t i = 0; i < payload_len; i++) payload[i] = (uint8_t)i;
  icmp->csum = 0;
  icmp->csum = checksum16(icmp, (uint16_t)(sizeof(icmp_hdr_t) + payload_len));

  icmp_wait_id = 0xBEEF;
  icmp_wait_seq = 1;
  icmp_reply_ok = 0;

  uint16_t total_len = (uint16_t)(sizeof(eth_hdr_t) + sizeof(ip_hdr_t) +
                                  sizeof(icmp_hdr_t) + payload_len);
  rtl_send(frame, total_len);

  uint32_t start = get_tick_count();
  uint32_t timeout_ticks = (timeout_ms + 9) / 10;
  uint32_t spins = 0;
  while (!icmp_reply_ok) {
    rtl_rx_poll();
    if ((get_tick_count() - start) > timeout_ticks) {
      return -1;
    }
    if (++spins > timeout_ms * 10000u) {
      return -1;
    }
    __asm__ volatile("hlt");
  }

  return 0;
}

void net_init(void) {
  printf("[net] init ip=%d.%d.%d.%d mask=%d.%d.%d.%d gw=%d.%d.%d.%d\n",
         net_ip[0], net_ip[1], net_ip[2], net_ip[3],
         net_mask[0], net_mask[1], net_mask[2], net_mask[3],
         net_gw[0], net_gw[1], net_gw[2], net_gw[3]);
  rtl_init();
}
