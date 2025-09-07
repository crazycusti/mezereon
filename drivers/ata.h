#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ATA_NONE = 0,
    ATA_ATA  = 1,
    ATA_ATAPI = 2,
} ata_type_t;

// Device descriptor used for scan results
typedef struct {
    uint16_t io;
    uint16_t ctrl;
    bool     slave;   // false=master, true=slave
    ata_type_t type;  // result of detection
} ata_dev_t;

// Select target device (default is primary master). Affects subsequent operations.
void ata_set_target(uint16_t io, uint16_t ctrl, bool slave);

// Probe currently selected target and return type
ata_type_t ata_detect(void);

// Scan 4 standard slots: [0]=PM, [1]=PS, [2]=SM, [3]=SS
void ata_scan(ata_dev_t out[4]);

// Return true if the currently selected target is a normal ATA disk
bool ata_present(void);

bool ata_init(void);
// Read up to `sectors` (1..4) 512B sectors starting at LBA into buf (must be >= sectors*512)
bool ata_read_lba28(uint32_t lba, uint8_t sectors, void* buf);
// Convenience: read up to 2 KiB from given LBA and hexdump
void ata_dump_lba(uint32_t lba, uint8_t sectors_max);

// Write up to `sectors` (1..4) 512B sectors starting at LBA from buf (must be >= sectors*512)
bool ata_write_lba28(uint32_t lba, uint8_t sectors, const void* buf);
