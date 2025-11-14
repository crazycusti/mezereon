#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

// Architecture identifiers
#define BI_ARCH_X86   1
#define BI_ARCH_SPARC 2

// E820-compatible memory map entry types (subset we care about)
#define BOOTINFO_MEMORY_TYPE_USABLE      1
#define BOOTINFO_MEMORY_TYPE_RESERVED    2
#define BOOTINFO_MEMORY_TYPE_ACPI        3
#define BOOTINFO_MEMORY_TYPE_NVS         4
#define BOOTINFO_MEMORY_TYPE_BAD         5

#define BOOTINFO_MEMORY_MAX_RANGES 32

// Generic boot flags shared across architectures
#define BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD 0x00000001u

typedef struct bootinfo_memory_range {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} bootinfo_memory_range_t;

typedef struct bootinfo_memory_map {
    uint32_t entry_count;
    bootinfo_memory_range_t entries[BOOTINFO_MEMORY_MAX_RANGES];
} bootinfo_memory_map_t;

// Minimal boot info structure, extensible per-arch
typedef struct boot_info {
    uint32_t arch;        // BI_ARCH_*
    uint32_t machine;     // optional machine id/platform
    const char* console;  // backend name, e.g. "vga_text"
    uint32_t flags;       // arch-specific flags (see BOOTINFO_FLAG_*)
    void*    prom;        // firmware/prom vector (e.g. SPARC OBP)
    uint32_t boot_device; // BIOS/firmware provided boot device identifier
    /* Framebuffer / VBE info (filled by bootloader if available) */
    uint16_t vbe_mode;        // preferred 8bpp VBE mode (if detected)
    uint16_t vbe_pitch;       // bytes per scanline for preferred mode
    uint16_t vbe_width;       // width in pixels for preferred mode
    uint16_t vbe_height;      // height in pixels for preferred mode
    uint8_t  vbe_bpp;         // bits per pixel for preferred mode (expect 8)
    uint8_t  _pad0;
    uint32_t framebuffer_phys; // physical address of preferred LFB (0 = none)

    /* Optional secondary 4bpp mode (banked or linear) */
    uint16_t vbe_mode_4bpp;      // preferred 4bpp mode (0 = none)
    uint16_t vbe_pitch_4bpp;     // pitch for 4bpp mode (bytes per scanline)
    uint16_t vbe_width_4bpp;     // width in pixels for 4bpp mode
    uint16_t vbe_height_4bpp;    // height in pixels for 4bpp mode
    uint8_t  vbe_bpp_4bpp;       // bits per pixel for 4bpp mode (expect 4)
    uint8_t  _pad1;
    uint32_t framebuffer_phys_4bpp; // physical address if linear framebuffer available (0 otherwise)

    uint32_t vbe_memory_bytes;   // total reported VBE video memory (bytes)
    bootinfo_memory_map_t memory;
} boot_info_t;

#endif // BOOTINFO_H
