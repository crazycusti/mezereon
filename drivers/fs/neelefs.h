#pragma once
#include <stdint.h>
#include <stdbool.h>

// Simple read-only NeeleFS living at a fixed LBA on an ATA device

typedef struct {
    char     name[32];
    uint32_t offset;   // byte offset from start of FS
    uint32_t size;     // bytes
    uint32_t checksum; // optional, 0 if unused
} neelefs_dirent_t;

bool neelefs_mount(uint32_t lba);
void neelefs_list(void);
bool neelefs_cat(const char* name);

