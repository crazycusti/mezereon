#pragma once
#include <stdint.h>
#include <stdbool.h>

// NeeleFS v1: simple read-only FS; NeeleFS v2: 16MB, dirs, write support

typedef struct {
    char     name[32];
    uint32_t offset;   // byte offset from start of FS
    uint32_t size;     // bytes
    uint32_t checksum; // optional, 0 if unused
} neelefs_dirent_t;

bool neelefs_mount(uint32_t lba);
void neelefs_list(void);
bool neelefs_cat(const char* name);

// v2 API (detected automatically on mount). 16MB region, 512B blocks.
// Paths use '/' separators, max depth 255, name length <=32.
bool neelefs_mkfs_16mb(uint32_t lba);
bool neelefs_mkfs_16mb_force(uint32_t lba);
bool neelefs_ls_path(const char* path);
bool neelefs_mkdir(const char* path);
bool neelefs_write_text(const char* path, const char* text);
bool neelefs_cat_path(const char* path);
bool neelefs_read_text(const char* path, char* out, uint32_t out_max, uint32_t* out_len);
// Verify integrity (CRC32): if path is a file, checks that file; if directory or '/', checks recursively.
// Verify integrity (CRC32): if path is a file, checks that file; if directory or '/', checks recursively.
// When verbose!=0, prints CRCs even for OK files.
bool neelefs_verify(const char* path, int verbose);
