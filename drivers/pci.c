#include "../config.h"
#include "pci.h"

#if CONFIG_ARCH_X86
#include "../arch/x86/io.h"
#endif

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static size_t g_pci_device_count = 0;

static void pci_store_device(uint8_t bus, uint8_t device, uint8_t function) {
    if (g_pci_device_count >= PCI_MAX_DEVICES) {
        return;
    }

    uint16_t vendor = pci_config_read16(bus, device, function, 0x00);
    if (vendor == 0xFFFF) {
        return;
    }

    uint16_t device_id = pci_config_read16(bus, device, function, 0x02);
    uint32_t class_reg = pci_config_read32(bus, device, function, 0x08);
    uint8_t revision = (uint8_t)(class_reg & 0xFF);
    uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    uint8_t class_id = (uint8_t)((class_reg >> 24) & 0xFF);

    uint32_t header_reg = pci_config_read32(bus, device, function, 0x0C);
    uint8_t header_type = (uint8_t)((header_reg >> 16) & 0xFF);
    uint8_t interrupt_line = (uint8_t)(pci_config_read32(bus, device, function, 0x3C) & 0xFF);

    pci_device_t *entry = &g_pci_devices[g_pci_device_count++];
    for (int i = 0; i < 6; i++) {
        entry->bars[i] = 0;
    }
    entry->bus = bus;
    entry->device = device;
    entry->function = function;
    entry->vendor_id = vendor;
    entry->device_id = device_id;
    entry->class_id = class_id;
    entry->subclass_id = subclass;
    entry->prog_if = prog_if;
    entry->revision_id = revision;
    entry->header_type = header_type;
    entry->interrupt_line = interrupt_line;

    uint8_t header_layout = header_type & 0x7F;
    uint8_t bar_count = 0;
    switch (header_layout) {
        case 0x00: bar_count = 6; break; // general device
        case 0x01: bar_count = 2; break; // PCI-to-PCI bridge
        case 0x02: bar_count = 0; break; // CardBus bridge (ignore for now)
        default:   bar_count = 0; break;
    }

    for (uint8_t i = 0; i < bar_count && i < 6; i++) {
        entry->bars[i] = pci_config_read32(bus, device, function, (uint8_t)(0x10 + i * 4));
    }
}

#if CONFIG_ARCH_X86
static uint32_t pci_cfg_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)(0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) | (offset & 0xFC));
}
#endif

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
#if CONFIG_ARCH_X86
    outl(0xCF8, pci_cfg_address(bus, device, function, offset));
    return inl(0xCFC);
#else
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xFFFFffffu;
#endif
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, (uint8_t)(offset & 0xFC));
    uint8_t shift = (offset & 2) * 8;
    return (uint16_t)((value >> shift) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, (uint8_t)(offset & 0xFC));
    uint8_t shift = (offset & 3) * 8;
    return (uint8_t)((value >> shift) & 0xFF);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
#if CONFIG_ARCH_X86
    outl(0xCF8, pci_cfg_address(bus, device, function, offset));
    outl(0xCFC, value);
#else
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
#endif
}

void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
#if CONFIG_ARCH_X86
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t combined = pci_config_read32(bus, device, function, (uint8_t)(offset & 0xFC));
    combined = (combined & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, device, function, (uint8_t)(offset & 0xFC), combined);
#else
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
#endif
}

void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
#if CONFIG_ARCH_X86
    uint32_t shift = (offset & 3) * 8;
    uint32_t mask = 0xFFu << shift;
    uint32_t combined = pci_config_read32(bus, device, function, (uint8_t)(offset & 0xFC));
    combined = (combined & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, device, function, (uint8_t)(offset & 0xFC), combined);
#else
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
#endif
}

void pci_init(void) {
    g_pci_device_count = 0;
#if CONFIG_ARCH_X86
    for (uint16_t bus = 0; bus < 256 && g_pci_device_count < PCI_MAX_DEVICES; bus++) {
        for (uint8_t device = 0; device < 32 && g_pci_device_count < PCI_MAX_DEVICES; device++) {
            uint16_t vendor = pci_config_read16((uint8_t)bus, device, 0, 0x00);
            if (vendor == 0xFFFF) {
                continue;
            }

            uint32_t header = pci_config_read32((uint8_t)bus, device, 0, 0x0C);
            pci_store_device((uint8_t)bus, device, 0);

            if (header & 0x00800000u) { // multifunction device
                for (uint8_t function = 1; function < 8 && g_pci_device_count < PCI_MAX_DEVICES; function++) {
                    uint16_t vendor_fn = pci_config_read16((uint8_t)bus, device, function, 0x00);
                    if (vendor_fn == 0xFFFF) {
                        continue;
                    }
                    pci_store_device((uint8_t)bus, device, function);
                }
            }
        }
    }
#endif
}

const pci_device_t* pci_get_devices(size_t* count) {
    if (count) {
        *count = g_pci_device_count;
    }
    return g_pci_devices;
}

pci_device_t* pci_find_device(uint16_t vendor, uint16_t device) {
    for (size_t i = 0; i < g_pci_device_count; i++) {
        pci_device_t* entry = &g_pci_devices[i];
        if (entry->vendor_id == vendor && entry->device_id == device) {
            return entry;
        }
    }
    return NULL;
}
