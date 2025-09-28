#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <stdint.h>
#include <stddef.h>

#define PCI_MAX_DEVICES 32

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_id;
    uint8_t  subclass_id;
    uint8_t  prog_if;
    uint8_t  revision_id;
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint32_t bars[6];
} pci_device_t;

void pci_init(void);
const pci_device_t* pci_get_devices(size_t* count);
pci_device_t* pci_find_device(uint16_t vendor, uint16_t device);
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);

#endif // DRIVERS_PCI_H
