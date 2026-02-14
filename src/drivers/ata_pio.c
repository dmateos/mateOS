#include "ata_pio.h"
#include "../arch/i686/io.h"

#define ATA_IO_BASE      0x1F0
#define ATA_CTRL_BASE    0x3F6

#define ATA_REG_DATA       (ATA_IO_BASE + 0)
#define ATA_REG_SECCOUNT0  (ATA_IO_BASE + 2)
#define ATA_REG_LBA0       (ATA_IO_BASE + 3)
#define ATA_REG_LBA1       (ATA_IO_BASE + 4)
#define ATA_REG_LBA2       (ATA_IO_BASE + 5)
#define ATA_REG_HDDEVSEL   (ATA_IO_BASE + 6)
#define ATA_REG_COMMAND    (ATA_IO_BASE + 7)
#define ATA_REG_STATUS     (ATA_IO_BASE + 7)

#define ATA_REG_ALTSTATUS  (ATA_CTRL_BASE + 0)

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY     0xEC

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF  0x20
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY 0x80

static int ata_ready = 0;

static void ata_delay_400ns(void) {
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
    (void)inb(ATA_REG_ALTSTATUS);
}

static int ata_wait_not_busy(uint32_t spins) {
    while (spins--) {
        uint8_t s = inb(ATA_REG_STATUS);
        if (!(s & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(uint32_t spins) {
    while (spins--) {
        uint8_t s = inb(ATA_REG_STATUS);
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

int ata_pio_init(void) {
    ata_ready = 0;

    // Select primary master
    outb(ATA_REG_HDDEVSEL, 0xA0);
    ata_delay_400ns();

    outb(ATA_REG_SECCOUNT0, 0);
    outb(ATA_REG_LBA0, 0);
    outb(ATA_REG_LBA1, 0);
    outb(ATA_REG_LBA2, 0);
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay_400ns();

    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0) {
        return -1;  // No device
    }

    // If LBA1/LBA2 are non-zero, this is likely ATAPI or unsupported here.
    uint8_t cl = inb(ATA_REG_LBA1);
    uint8_t ch = inb(ATA_REG_LBA2);
    if (cl != 0 || ch != 0) {
        return -1;
    }

    if (ata_wait_drq(1000000) < 0) {
        return -1;
    }

    // Read IDENTIFY data (256 words)
    for (int i = 0; i < 256; i++) {
        (void)inw(ATA_REG_DATA);
    }

    ata_ready = 1;
    return 0;
}

int ata_pio_read(uint32_t lba, uint8_t count, void *buf) {
    if (!ata_ready || !buf || count == 0) return -1;

    // 28-bit LBA only in this minimal implementation
    if (lba & 0xF0000000u) return -1;

    if (ata_wait_not_busy(1000000) < 0) return -1;

    outb(ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_REG_SECCOUNT0, count);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    ata_delay_400ns();

    uint16_t *out = (uint16_t *)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(1000000) < 0) return -1;
        for (int i = 0; i < 256; i++) {
            out[s * 256 + i] = inw(ATA_REG_DATA);
        }
    }

    return 0;
}

int ata_pio_write(uint32_t lba, uint8_t count, const void *buf) {
    if (!ata_ready || !buf || count == 0) return -1;
    if (lba & 0xF0000000u) return -1;

    if (ata_wait_not_busy(1000000) < 0) return -1;

    outb(ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_REG_SECCOUNT0, count);
    outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    ata_delay_400ns();

    const uint16_t *in = (const uint16_t *)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(1000000) < 0) return -1;
        for (int i = 0; i < 256; i++) {
            outw(ATA_REG_DATA, in[s * 256 + i]);
        }
    }

    // Ensure command is fully completed.
    if (ata_wait_not_busy(1000000) < 0) return -1;
    return 0;
}

int ata_pio_is_ready(void) {
    return ata_ready;
}
