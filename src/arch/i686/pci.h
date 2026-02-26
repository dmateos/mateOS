#ifndef _PCI_H
#define _PCI_H

#include "lib.h"

// PCI config space I/O ports
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

// PCI config space register offsets
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_STATUS         0x06
#define PCI_CLASS_REV      0x08
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_BAR1           0x14
#define PCI_BAR2           0x18
#define PCI_BAR3           0x1C
#define PCI_BAR4           0x20
#define PCI_BAR5           0x24
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN  0x3D

// PCI Command register bits
#define PCI_CMD_IO_SPACE     0x0001
#define PCI_CMD_MEM_SPACE    0x0002
#define PCI_CMD_BUS_MASTER   0x0004

// Discovered PCI device
typedef struct {
  uint8_t  bus;
  uint8_t  device;
  uint8_t  function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t  class_code;
  uint8_t  subclass;
  uint8_t  irq_line;
  uint32_t bar[6];
} pci_device_t;

#define PCI_MAX_DEVICES 32

// Config space access
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
void     pci_config_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t value);

// Scan PCI bus and populate device list
void pci_init(void);

// Find a device by vendor/device ID, returns NULL if not found
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);

// Enable bus mastering for a device (required for DMA)
void pci_enable_bus_mastering(pci_device_t *dev);

// Print discovered PCI devices to console
void pci_list(void);
int pci_get_devices(pci_device_t *out, int max);

#endif
