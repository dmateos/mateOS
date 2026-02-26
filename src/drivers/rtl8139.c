#include "rtl8139.h"
#include "arch/i686/pci.h"
#include "arch/i686/io.h"
#include "arch/i686/interrupts.h"

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
#define RTL_INT_RER  0x02
#define RTL_INT_TOK  0x04
#define RTL_INT_RXOVW 0x10
#define RTL_INT_FOVW  0x40

#define RTL_RCR_AAP  0x01
#define RTL_RCR_APM  0x02
#define RTL_RCR_AM   0x04
#define RTL_RCR_AB   0x08
#define RTL_RCR_WRAP 0x80

#define RX_BUF_LEN 8192
#define RX_BUF_PAD (16 + 1500)
#define RX_BUF_SIZE (RX_BUF_LEN + RX_BUF_PAD)
#define TX_BUF_SIZE 2048
#define TX_DESC_COUNT 4

static uint16_t rtl_io = 0;
static uint8_t rtl_irq = 0;

static uint8_t rtl_mac[6] = {0};
static uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));
static uint16_t rx_offset = 0;  // Logical read pointer in 8KB RX ring
static uint8_t tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(16)));
static int tx_cur = 0;
static uint32_t rx_packets = 0;
static uint32_t tx_packets = 0;

static nic_rx_callback_t rx_callback = 0;

// ---- Send raw Ethernet frame ----
void rtl8139_send(const uint8_t *data, uint16_t len) {
  if (!rtl_io || len > TX_BUF_SIZE) return;
  int idx = tx_cur % TX_DESC_COUNT;
  memcpy(tx_buf[idx], data, len);
  outl(rtl_io + RTL_TSAD0 + (idx * 4), (uint32_t)tx_buf[idx]);
  outl(rtl_io + RTL_TSD0 + (idx * 4), len);
  tx_cur = (tx_cur + 1) % TX_DESC_COUNT;
  tx_packets++;
}

// ---- RX polling ----
void rtl8139_rx_poll(void) {
  if (!rtl_io) return;

  // Poll until RX buffer empty (CR bit0 = BUFE).
  while ((inb(rtl_io + RTL_CR) & 0x01) == 0) {
    uint16_t hdr_off = rx_offset;
    uint16_t status = *(uint16_t *)(rx_buf + hdr_off);
    uint16_t length = *(uint16_t *)(rx_buf + hdr_off + 2);

    // status bit0 = ROK, length includes CRC bytes.
    if (!(status & 0x01) || length < 4 || length > (RX_BUF_LEN + 4)) {
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
      uint16_t first = (uint16_t)(RX_BUF_LEN - data_off);
      memcpy(pkt, rx_buf + data_off, first);
      memcpy(pkt + first, rx_buf, (uint16_t)(pkt_len - first));
    }

    if (rx_callback) {
      rx_callback(pkt, pkt_len);
    }
    rx_packets++;

    rx_offset = (uint16_t)(rx_offset + length + 4);
    rx_offset = (uint16_t)((rx_offset + 3) & ~3);
    rx_offset %= RX_BUF_LEN;

    // CAPR should always track read ptr - 16 inside the 8KB ring.
    outw(rtl_io + RTL_CAPR, (uint16_t)((rx_offset - 16) & 0x1FFF));
  }
}

// ---- IRQ handler ----
static void rtl_irq_handler(uint32_t irq __attribute__((unused)),
                            uint32_t err __attribute__((unused))) {
  if (!rtl_io) return;
  uint16_t isr = inw(rtl_io + RTL_ISR);
  if (!isr) return;
  outw(rtl_io + RTL_ISR, isr);

  if (isr & (RTL_INT_ROK | RTL_INT_RER | RTL_INT_RXOVW | RTL_INT_FOVW)) {
    rtl8139_rx_poll();
  }
}

// ---- Init ----
void rtl8139_init(nic_rx_callback_t rx_cb) {
  rx_callback = rx_cb;

  pci_device_t *dev = pci_find_device(RTL_VENDOR_ID, RTL_DEVICE_ID);
  if (!dev) {
    kprintf("[rtl8139] not found\n");
    return;
  }

  if (!(dev->bar[0] & 0x01)) {
    kprintf("[rtl8139] BAR0 not IO\n");
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
  rx_offset = 0;
  outw(rtl_io + RTL_CAPR, 0xFFF0);

  outb(rtl_io + RTL_CR, RTL_CR_RX_EN | RTL_CR_TX_EN);

  outl(rtl_io + RTL_RCR, RTL_RCR_AB | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_WRAP);

  outw(rtl_io + RTL_IMR, RTL_INT_ROK | RTL_INT_RER |
                        RTL_INT_TOK | RTL_INT_RXOVW | RTL_INT_FOVW);

  if (rtl_irq != 0 && rtl_irq != 0xFF) {
    register_interrupt_handler((uint8_t)(0x20 + rtl_irq), rtl_irq_handler);
    pic_unmask_irq(rtl_irq);
  }

  kprintf("[rtl8139] io=0x%x irq=%d mac=%x:%x:%x:%x:%x:%x\n",
          rtl_io, rtl_irq,
          rtl_mac[0], rtl_mac[1], rtl_mac[2],
          rtl_mac[3], rtl_mac[4], rtl_mac[5]);
}

int rtl8139_available(void) {
  return rtl_io != 0;
}

void rtl8139_get_mac(uint8_t mac[6]) {
  memcpy(mac, rtl_mac, 6);
}

void rtl8139_get_stats(uint32_t *rx, uint32_t *tx) {
  if (rx) *rx = rx_packets;
  if (tx) *tx = tx_packets;
}
