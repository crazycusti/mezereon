#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "stage3_params.h"
#include "bootinfo.h"

static volatile uint16_t* const VGA_TEXT = (uint16_t*)0xB8000;
static uint32_t g_vga_pos = 0;
static const char g_console_name[] = "vga_text";
static const char HEX_DIGITS[] = "0123456789ABCDEF";

static void *stage3_memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)value;
    }
    return dst;
}

static void stage3_console_putc(char c) {
    if (c == '\n') {
        g_vga_pos += (80 - (g_vga_pos % 80));
    } else {
        VGA_TEXT[g_vga_pos++] = (uint16_t)c | 0x0700u;
    }
    if (g_vga_pos >= 80u * 25u) {
        g_vga_pos = 0;
    }
}

static void stage3_console_write(const char *s) {
    while (s && *s) {
        stage3_console_putc(*s++);
    }
}

static void stage3_print_hex8(uint8_t value) {
    stage3_console_putc(HEX_DIGITS[(value >> 4) & 0x0Fu]);
    stage3_console_putc(HEX_DIGITS[value & 0x0Fu]);
}

static void stage3_print_hex32(uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        stage3_console_putc(HEX_DIGITS[(value >> shift) & 0x0Fu]);
    }
}

static void stage3_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void stage3_panic(const char *msg) {
    stage3_console_putc('F');
    stage3_console_write(msg);
    stage3_halt();
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "d"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "d"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" :: "a"(value), "d"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "d"(port));
    return value;
}

#define ATA_REG_DATA      0
#define ATA_REG_ERROR     1
#define ATA_REG_SECCNT    2
#define ATA_REG_LBA0      3
#define ATA_REG_LBA1      4
#define ATA_REG_LBA2      5
#define ATA_REG_DRIVE     6
#define ATA_REG_STATUS    7
#define ATA_REG_COMMAND   7

#define ATA_REG_DEVCTRL   0
#define ATA_REG_ALTSTATUS 0

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DREQ 0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_PIO  0x20

static uint16_t g_ata_io_base = 0x1F0;
static uint16_t g_ata_ctrl_base = 0x3F6;
static uint8_t  g_ata_drive_select = 0xE0; // master by default

static void ata_delay(void) {
    (void)inb(g_ata_ctrl_base + ATA_REG_ALTSTATUS);
    (void)inb(g_ata_ctrl_base + ATA_REG_ALTSTATUS);
    (void)inb(g_ata_ctrl_base + ATA_REG_ALTSTATUS);
    (void)inb(g_ata_ctrl_base + ATA_REG_ALTSTATUS);
}

static bool ata_wait_bsy_clear(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        uint8_t status = inb(g_ata_io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq_set(void) {
    for (uint32_t i = 0; i < 100000; ++i) {
        uint8_t status = inb(g_ata_io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return false;
        }
        if (status & ATA_SR_DREQ) {
            return true;
        }
    }
    return false;
}

static void ata_select_device(uint8_t bios_drive) {
    if (bios_drive & 0x02u) {
        g_ata_io_base = 0x170;
        g_ata_ctrl_base = 0x376;
    } else {
        g_ata_io_base = 0x1F0;
        g_ata_ctrl_base = 0x3F6;
    }
    g_ata_drive_select = (bios_drive & 0x01u) ? 0xF0u : 0xE0u;
}

static bool ata_read_lba28(uint32_t lba, uint8_t sectors, void *buffer) {
    if (sectors == 0) {
        return true;
    }
    if (sectors > 4) {
        sectors = 4;
    }
    if (!ata_wait_bsy_clear()) {
        return false;
    }

    outb(g_ata_ctrl_base + ATA_REG_DEVCTRL, 0x02); // disable IRQs
    outb(g_ata_io_base + ATA_REG_DRIVE, (uint8_t)(g_ata_drive_select | ((lba >> 24) & 0x0Fu)));
    ata_delay();

    outb(g_ata_io_base + ATA_REG_SECCNT, sectors);
    outb(g_ata_io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFFu));
    outb(g_ata_io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
    outb(g_ata_io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
    outb(g_ata_io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *dst = (uint16_t *)buffer;
    for (uint8_t s = 0; s < sectors; ++s) {
        if (!ata_wait_bsy_clear()) {
            return false;
        }
        if (!ata_wait_drq_set()) {
            return false;
        }
        for (int i = 0; i < 256; ++i) {
            dst[i] = inw(g_ata_io_base + ATA_REG_DATA);
        }
        dst += 256;
    }
    return true;
}

static bool stage3_load_kernel(const stage3_params_t *params) {
    uint32_t remaining = params->kernel_sectors;
    uint32_t lba = params->kernel_lba;
    uint8_t *dest = (uint8_t *)(uintptr_t)params->kernel_load_linear;

    if (remaining == 0) {
        return false;
    }

    while (remaining > 0) {
        uint8_t chunk = (remaining > 4u) ? 4u : (uint8_t)remaining;
        stage3_console_write("kernel LBA=0x");
        stage3_print_hex32(lba);
        stage3_console_write(" count=0x");
        stage3_print_hex8(chunk);
        stage3_console_write("\n");
        if (!ata_read_lba28(lba, chunk, dest)) {
            return false;
        }
        lba += chunk;
        dest += (uint32_t)chunk * 512u;
        remaining -= chunk;
    }
    return true;
}

static void stage3_build_bootinfo(const stage3_params_t *params, boot_info_t *bi) {
    if (!bi) {
        return;
    }
    stage3_memset(bi, 0, sizeof(*bi));
    bi->arch = BI_ARCH_X86;
    bi->machine = 0;
    bi->console = g_console_name;
    bi->flags = (params->boot_drive >= 0x80u) ? BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD : 0u;
    bi->prom = NULL;
    bi->boot_device = params->boot_drive;

    bootinfo_memory_map_t *map = &bi->memory;
    map->entry_count = 0;

    const stage3_e820_entry_t *src = (const stage3_e820_entry_t *)(uintptr_t)params->e820_ptr;
    uint32_t count = params->e820_count;
    if (count > BOOTINFO_MEMORY_MAX_RANGES) {
        count = BOOTINFO_MEMORY_MAX_RANGES;
    }
    for (uint32_t i = 0; i < count; ++i) {
        map->entries[i].base = src[i].base;
        map->entries[i].length = src[i].length;
        map->entries[i].type = src[i].type;
        map->entries[i].attr = src[i].attr;
    }
    map->entry_count = count;
}

void stage3_main(stage3_params_t *params, boot_info_t *bootinfo) {
    if (!params || !bootinfo) {
        stage3_panic("param");
    }

    stage3_console_write("S3: drive=0x");
    stage3_print_hex8((uint8_t)params->boot_drive);
    stage3_console_write(" stage3_lba=0x");
    stage3_print_hex32(params->stage3_lba);
    stage3_console_write(" stage3_secs=0x");
    stage3_print_hex32(params->stage3_sectors);
    stage3_console_write(" kernel_lba=0x");
    stage3_print_hex32(params->kernel_lba);
    stage3_console_write(" kernel_secs=0x");
    stage3_print_hex32(params->kernel_sectors);
    stage3_console_write("\n");

    if (params->boot_drive < 0x80u) {
        stage3_panic("flpy");
    }

    ata_select_device((uint8_t)params->boot_drive);
    stage3_console_write("ATA selected\n");

    if ((uint32_t)(uintptr_t)bootinfo != params->bootinfo_ptr) {
        stage3_panic("bi");
    }

    stage3_console_putc('K');
    if (!stage3_load_kernel(params)) {
        stage3_panic("load");
    }
    stage3_console_putc('k');
    stage3_console_write("S3: kernel loaded, jumping\n");

    stage3_build_bootinfo(params, bootinfo);

    void (*kernel_entry)(void) = (void (*)(void))(uintptr_t)params->kernel_load_linear;
    kernel_entry();

    stage3_panic("return");
}
