#include "pci.h"
#include "io.h"

static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn,
                           uint8_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) | ((uint32_t)fn << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fn,
                           uint8_t offset) {
    uint32_t val = pci_config_read32(bus, dev, fn, offset);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    uint32_t val = pci_config_read32(bus, dev, fn, offset);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset,
                        uint16_t value) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) |
                       ((uint32_t)dev << 11) | ((uint32_t)fn << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);

    // Read-modify-write: read the full 32-bit dword, replace the 16-bit portion
    uint32_t old = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFF << shift;
    uint32_t new_val = (old & ~mask) | ((uint32_t)value << shift);

    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, new_val);
}

static void pci_scan_device(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint16_t vendor = pci_config_read16(bus, dev, fn, PCI_VENDOR_ID);
    if (vendor == 0xFFFF)
        return;

    if (pci_device_count >= PCI_MAX_DEVICES)
        return;

    pci_device_t *d = &pci_devices[pci_device_count];
    d->bus = bus;
    d->device = dev;
    d->function = fn;
    d->vendor_id = vendor;
    d->device_id = pci_config_read16(bus, dev, fn, PCI_DEVICE_ID);

    uint32_t class_rev = pci_config_read32(bus, dev, fn, PCI_CLASS_REV);
    d->class_code = (class_rev >> 24) & 0xFF;
    d->subclass = (class_rev >> 16) & 0xFF;

    d->irq_line = pci_config_read8(bus, dev, fn, PCI_INTERRUPT_LINE);

    for (int i = 0; i < 6; i++) {
        d->bar[i] = pci_config_read32(bus, dev, fn, PCI_BAR0 + i * 4);
    }

    kprintf("  [pci] %d:%d.%d ", bus, dev, fn);
    kprintf("vendor=%x device=%x ", d->vendor_id, d->device_id);
    kprintf("class=%x.%x", d->class_code, d->subclass);
    if (d->irq_line && d->irq_line != 0xFF) {
        kprintf(" irq=%d", d->irq_line);
    }
    // Only print I/O BARs (bit 0 set) since printf can't handle large unsigned
    if (d->bar[0] & 0x01) {
        kprintf(" iobar=0x%x", d->bar[0] & 0xFFFC);
    }
    kprintf("\n");

    pci_device_count++;
}

void pci_init(void) {
    kprintf("PCI bus scan...\n");
    pci_device_count = 0;

    for (int dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(0, dev, 0, PCI_VENDOR_ID);
        if (vendor == 0xFFFF)
            continue;

        pci_scan_device(0, dev, 0);

        // Check for multi-function device
        uint8_t header_type = pci_config_read8(0, dev, 0, PCI_HEADER_TYPE);
        if (header_type & 0x80) {
            for (int fn = 1; fn < 8; fn++) {
                pci_scan_device(0, dev, fn);
            }
        }
    }

    kprintf("PCI: %d devices found\n", pci_device_count);
}

pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_enable_bus_mastering(pci_device_t *dev) {
    uint16_t cmd =
        pci_config_read16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE;
    pci_config_write16(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
}

void pci_list(void) {
    kprintf("PCI devices (%d):\n", pci_device_count);
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *d = &pci_devices[i];
        kprintf("  %d:%d.%d vendor=%x device=%x class=%x.%x", d->bus, d->device,
                d->function, d->vendor_id, d->device_id, d->class_code,
                d->subclass);
        if (d->irq_line && d->irq_line != 0xFF) {
            kprintf(" irq=%d", d->irq_line);
        }
        if (d->bar[0] & 0x01) {
            kprintf(" iobar=0x%x", d->bar[0] & 0xFFFC);
        }
        kprintf("\n");
    }
}

int pci_get_devices(pci_device_t *out, int max) {
    if (!out || max <= 0)
        return 0;
    int count = (pci_device_count < max) ? pci_device_count : max;
    for (int i = 0; i < count; i++) {
        out[i] = pci_devices[i];
    }
    return count;
}
