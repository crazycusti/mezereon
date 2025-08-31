#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ATA_NONE = 0,
    ATA_ATA  = 1,
    ATA_ATAPI = 2,
} ata_type_t;

// Probe the primary master and return type
ata_type_t ata_detect(void);
// Return true if a normal ATA disk is present (cached result after detect/init)
bool ata_present(void);

bool ata_init(void);
// Read up to `sectors` (1..4) 512B sectors starting at LBA into buf (must be >= sectors*512)
bool ata_read_lba28(uint32_t lba, uint8_t sectors, void* buf);
// Convenience: read up to 2 KiB from given LBA and hexdump
void ata_dump_lba(uint32_t lba, uint8_t sectors_max);
