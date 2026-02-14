#ifndef _ATA_PIO_H
#define _ATA_PIO_H

#include "../lib.h"

// Initialize primary-master ATA PIO device.
// Returns 0 on success, -1 if no ATA disk is available.
int ata_pio_init(void);

// Read 'count' 512-byte sectors starting at LBA into buf.
// Returns 0 on success, -1 on error.
int ata_pio_read(uint32_t lba, uint8_t count, void *buf);

// Write 'count' 512-byte sectors starting at LBA from buf.
// Returns 0 on success, -1 on error.
int ata_pio_write(uint32_t lba, uint8_t count, const void *buf);

// Returns 1 if an ATA disk was successfully initialized, else 0.
int ata_pio_is_ready(void);

#endif
