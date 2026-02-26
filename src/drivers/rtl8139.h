#ifndef _RTL8139_H
#define _RTL8139_H

#include "lib.h"

// Receive callback type — driver calls this for each received frame
typedef void (*nic_rx_callback_t)(uint8_t *data, uint16_t len);

void rtl8139_init(nic_rx_callback_t rx_cb);
int  rtl8139_available(void);
void rtl8139_send(const uint8_t *data, uint16_t len);
void rtl8139_rx_poll(void);
void rtl8139_get_mac(uint8_t mac[6]);
void rtl8139_get_stats(uint32_t *rx_packets, uint32_t *tx_packets);

#endif
