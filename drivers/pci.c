#include "../config.h"
#include "pci.h"
#include "../console.h"

#if CONFIG_ARCH_X86
#include "../arch/x86/io.h"
#endif

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static size_t g_pci_device_count = 0;

static void pci_write_u64_hex(uint64_t value) {
    uint32_t hi = (uint32_t)(value >> 32);
    uint32_t lo = (uint32_t)value;
    if (hi) {
        console_write_hex32(hi);
        console_write_hex32(lo);
    } else {
        console_write_hex32(lo);
    }
}

static void pci_log_pretty_size(uint64_t bytes) {
    const uint64_t one_kib = 1024ULL;
    const uint64_t one_mib = one_kib * 1024ULL;
    const uint64_t one_gib = one_mib * 1024ULL;

    if (bytes == 0) {
        console_write("unknown");
        return;
    }

    if (bytes % one_gib == 0 && (bytes / one_gib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_gib));
        console_write(" GiB");
        return;
    }
    if (bytes % one_mib == 0 && (bytes / one_mib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_mib));
        console_write(" MiB");
        return;
    }
    if (bytes % one_kib == 0 && (bytes / one_kib) <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)(bytes / one_kib));
        console_write(" KiB");
        return;
    }
    if (bytes <= 0xFFFFFFFFULL) {
        console_write_dec((uint32_t)bytes);
        console_write(" bytes");
        return;
    }
    console_write("0x");
    pci_write_u64_hex(bytes);
    console_write(" bytes");
}

static uint64_t pci_probe_bar_size(uint8_t bus,
                                   uint8_t device,
                                   uint8_t function,
                                   uint8_t offset,
                                   uint32_t raw_low,
                                   uint32_t raw_high,
                                   int has_high) {
    int is_io = (raw_low & 0x1) ? 1 : 0;
    int type = (int)((raw_low >> 1) & 0x3);
    int is_mem64 = (!is_io) && (type == 0x2) && has_high;

    uint32_t mask_low;
    uint32_t mask_high = 0;

    pci_config_write32(bus, device, function, offset, 0xFFFFFFFFu);
    mask_low = pci_config_read32(bus, device, function, offset);

    if (is_mem64) {
        pci_config_write32(bus, device, function, (uint8_t)(offset + 4), 0xFFFFFFFFu);
        mask_high = pci_config_read32(bus, device, function, (uint8_t)(offset + 4));
    }

    pci_config_write32(bus, device, function, offset, raw_low);
    if (is_mem64) {
        pci_config_write32(bus, device, function, (uint8_t)(offset + 4), raw_high);
    }

    if (is_io) {
        mask_low &= ~0x3u;
        if (mask_low == 0 || mask_low == 0xFFFFFFFFu) {
            return 0;
        }
        uint32_t size = (~mask_low) + 1u;
        return (uint64_t)size;
    }

    mask_low &= ~0xFu;
    if (is_mem64) {
        uint64_t mask = mask_low;
        mask |= ((uint64_t)mask_high << 32);
        if (mask == 0 || mask == UINT64_MAX) {
            return 0;
        }
        return (~mask) + 1ULL;
    }

    if (mask_low == 0 || mask_low == 0xFFFFFFFFu) {
        return 0;
    }
    uint32_t size = (~mask_low) + 1u;
    return (uint64_t)size;
}

static void pci_log_bar(const pci_bar_info_t* bar, uint8_t index) {
    if (!bar) {
        return;
    }
    if (bar->raw == 0 || bar->raw == 0xFFFFFFFFu) {
        return;
    }

    console_write("    BAR");
    console_write_dec(index);
    console_write(": ");

    if (bar->raw & 0x1u) {
        console_write("IO   ");
    } else {
        uint32_t type = (bar->raw >> 1) & 0x3u;
        switch (type) {
            case 0x0: console_write("MEM32"); break;
            case 0x2: console_write("MEM64"); break;
            case 0x1: console_write("MEM16"); break;
            default:  console_write("MEM??"); break;
        }
        console_write((bar->raw & 0x8u) ? " pref " : " nonpref ");
    }

    console_write("base=0x");
    pci_write_u64_hex(bar->base);
    console_write(" size=");
    pci_log_pretty_size(bar->size);
    console_write(" (0x");
    pci_write_u64_hex(bar->size);
    console_writeln(")");
}

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
        entry->bars[i].raw = 0;
        entry->bars[i].raw_upper = 0;
        entry->bars[i].base = 0;
        entry->bars[i].size = 0;
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

    uint8_t i = 0;
    while (i < bar_count && i < 6) {
        uint8_t offset = (uint8_t)(0x10 + i * 4);
        uint32_t raw = pci_config_read32(bus, device, function, offset);
        entry->bars[i].raw = raw;
        entry->bars[i].raw_upper = 0;
        entry->bars[i].base = 0;
        entry->bars[i].size = 0;

        if (raw == 0 || raw == 0xFFFFFFFFu) {
            ++i;
            continue;
        }

        int is_io = (raw & 0x1) ? 1 : 0;
        int type = (int)((raw >> 1) & 0x3);
        uint32_t raw_high = 0;
        int has_high = 0;

        if (!is_io && type == 0x2 && (i + 1) < bar_count && (i + 1) < 6) {
            raw_high = pci_config_read32(bus, device, function, (uint8_t)(offset + 4));
            entry->bars[i].raw_upper = raw_high;
            has_high = 1;
        }

        if (is_io) {
            entry->bars[i].base = (uint64_t)(raw & ~0x3u);
        } else {
            uint64_t base = (uint64_t)(raw & ~0xFu);
            if (type == 0x2 && has_high) {
                base |= ((uint64_t)raw_high << 32);
            }
            entry->bars[i].base = base;
        }

        entry->bars[i].size = pci_probe_bar_size(bus, device, function, offset, raw, raw_high, has_high);

        if (!is_io && type == 0x2) {
            if (has_high && (i + 1) < 6) {
                entry->bars[i + 1].raw = raw_high;
                entry->bars[i + 1].raw_upper = 0;
                entry->bars[i + 1].base = 0;
                entry->bars[i + 1].size = 0;
            }
            i = (uint8_t)(i + (has_high ? 2 : 1));
        } else {
            ++i;
        }
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

void pci_log_summary(void) {
    size_t count = 0;
    const pci_device_t* devices = pci_get_devices(&count);
    if (!devices || count == 0) {
        console_writeln("PCI: no devices enumerated.");
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const pci_device_t* dev = &devices[i];
        console_write("PCI[");
        console_write_dec((uint32_t)i);
        console_write("] bus=");
        console_write_dec(dev->bus);
        console_write(" dev=");
        console_write_dec(dev->device);
        console_write(" fn=");
        console_write_dec(dev->function);
        console_write(" vendor=0x");
        console_write_hex16(dev->vendor_id);
        console_write(" device=0x");
        console_write_hex16(dev->device_id);
        console_write(" class=0x");
        console_write_hex16((uint16_t)(((uint16_t)dev->class_id << 8) | dev->subclass_id));
        console_write(" prog-if=0x");
        console_write_hex16(dev->prog_if);
        console_write(" rev=0x");
        console_write_hex16(dev->revision_id);
        console_write(" irq=");
        if (dev->interrupt_line == 0xFF) {
            console_write("none");
        } else {
            console_write_dec(dev->interrupt_line);
        }
        console_writeln("");

        for (uint8_t bar = 0; bar < 6; ++bar) {
            pci_log_bar(&dev->bars[bar], bar);
        }
    }
}
