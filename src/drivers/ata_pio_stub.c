/*
 * ata_pio_stub.c — ATA PIO stubs for x86_64 build
 * Satisfies fat16.c link dependencies. Disk access is a TODO for x86_64.
 */
#ifndef ARCH_I686
#include "drivers/ata_pio.h"
#include "lib.h"

int ata_pio_init(void) {
    kprintf("[ata_pio] x86_64 stub — disk access not implemented\n");
    return -1;
}
int ata_pio_read(uint32_t lba, uint8_t count, void *buf) {
    (void)lba; (void)count; (void)buf;
    return -1;
}
int ata_pio_write(uint32_t lba, uint8_t count, const void *buf) {
    (void)lba; (void)count; (void)buf;
    return -1;
}
#endif
