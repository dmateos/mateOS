/*
 * rtl8139_stub.c — RTL8139 stub for x86_64 build
 */
#ifndef ARCH_I686
#include "drivers/rtl8139.h"

void rtl8139_init(nic_rx_callback_t rx_cb) { (void)rx_cb; }
void rtl8139_send(const uint8_t *data, uint16_t len) { (void)data; (void)len; }
void rtl8139_rx_poll(void) {}
void rtl8139_get_mac(uint8_t mac[6]) { (void)mac; }
#endif
