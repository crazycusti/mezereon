#ifndef STAGE3_PARAMS_H
#define STAGE3_PARAMS_H

#include <stdint.h>

#define STAGE3_FLAG_USE_LBA 0x00000001u
#define STAGE3_FLAG_KERNEL_PRELOADED 0x00000002u

typedef struct __attribute__((packed)) stage3_params {
    uint32_t boot_drive;
    uint32_t stage3_lba;
    uint32_t stage3_sectors;
    uint32_t flags;
    uint16_t sectors_per_track;
    uint16_t heads_per_cyl;
    uint32_t stage3_load_linear;
    uint32_t bootinfo_ptr;
    uint32_t kernel_lba;
    uint32_t kernel_sectors;
    uint32_t kernel_load_linear;
    uint32_t kernel_buffer_linear;
    uint32_t e820_ptr;
    uint32_t e820_count;
} stage3_params_t;

typedef struct __attribute__((packed)) stage3_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} stage3_e820_entry_t;

#endif // STAGE3_PARAMS_H
