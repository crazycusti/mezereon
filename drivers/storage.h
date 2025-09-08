#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ata.h"

typedef struct {
    ata_dev_t dev;          // io/ctrl/slave/type
    int       present;      // 1 if ATA disk present (ATA_ATA)
    int       neelefs_found;// 1 if NeeleFS magic found at a known LBA
    int       neelefs_ver;  // 0=none, 1=v1, 2=v2
    uint32_t  neelefs_lba;  // 0 or 2048 when found
    int       mounted;      // 1 if currently mounted via global NeeleFS
} storage_info_t;

// Scan ATA (PM/PS/SM/SS). Populates internal table and attempts no mount.
void storage_scan(void);

// Return number of scanned entries (max 4).
int storage_count(void);

// Copy info for index (0..count-1). Returns 0 on out-of-range.
int storage_get(int idx, storage_info_t* out);

// Try to automount the first detected NeeleFS (preference: v2@2048, v2@0, v1@2048, v1@0).
// Returns index mounted or -1 if none mounted.
int storage_automount(void);

// Attempt to mount NeeleFS for a given scanned index using its detected LBA. Returns 1 on success.
int storage_mount_index(int idx);

