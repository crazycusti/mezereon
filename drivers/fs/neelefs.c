#include "neelefs.h"
#include "../../config.h"
#include "../../main.h"
#include "../../console.h"
#include "../ata.h"
#include <stdint.h>

#define NEELEFS_MAGIC_STR "NEELEFS1"

static uint32_t g_mount_lba = 0;
static int g_mounted = 0;
static uint32_t g_count = 0;
static uint32_t g_table_bytes = 0;

static int str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

bool neelefs_mount(uint32_t lba) {
    // Read first sector
    uint8_t sec[512];
    if (!ata_read_lba28(lba, 1, sec)) { console_writeln("NeeleFS: read failed"); return false; }
    // Check magic
    const char* m = (const char*)sec;
    for (int i=0; i<8; i++) {
        if (m[i] != NEELEFS_MAGIC_STR[i]) { console_writeln("NeeleFS: bad magic"); return false; }
    }
    // file count and table bytes
    uint32_t* p32 = (uint32_t*)(sec + 8);
    g_count = p32[0];
    g_table_bytes = p32[1];
    if (g_table_bytes == 0) g_table_bytes = g_count * sizeof(neelefs_dirent_t);
    g_table_bytes = align_up(g_table_bytes + 16, 512) - 16; // account header in sector 0
    g_mount_lba = lba;
    g_mounted = 1;
    console_write("NeeleFS mounted at LBA "); console_write_hex16((uint16_t)(lba & 0xFFFF)); console_write("\n");
    return true;
}

static bool neelefs_read_dirent(uint32_t index, neelefs_dirent_t* out) {
    if (index >= g_count) return false;
    uint32_t header_bytes = 16; // magic(8) + count(4) + table_bytes(4)
    uint32_t offset = header_bytes + index * sizeof(neelefs_dirent_t);
    uint32_t abs = offset;
    uint32_t lba = g_mount_lba + (abs / 512);
    uint32_t off = abs % 512;
    uint8_t sec[512];
    if (!ata_read_lba28(lba, 1, sec)) return false;
    if (off + sizeof(neelefs_dirent_t) <= 512) {
        const uint8_t* p = sec + off;
        for (unsigned i=0;i<sizeof(neelefs_dirent_t);i++) ((uint8_t*)out)[i] = p[i];
        return true;
    } else {
        // crosses sector boundary; read next too
        uint8_t sec2[512];
        if (!ata_read_lba28(lba+1, 1, sec2)) return false;
        unsigned first = 512 - off;
        for (unsigned i=0;i<first;i++) ((uint8_t*)out)[i] = sec[off+i];
        for (unsigned i=0;i<sizeof(neelefs_dirent_t)-first;i++) ((uint8_t*)out)[first+i] = sec2[i];
        return true;
    }
}

void neelefs_list(void) {
    if (!g_mounted) { console_writeln("NeeleFS not mounted"); return; }
    for (uint32_t i=0; i<g_count; i++) {
        neelefs_dirent_t e; if (!neelefs_read_dirent(i, &e)) break;
        console_write(" "); console_write(e.name); console_write("  ");
        // size
        console_write_dec(e.size); console_write(" bytes\n");
    }
}

static int find_entry(const char* name, neelefs_dirent_t* out) {
    for (uint32_t i=0; i<g_count; i++) {
        neelefs_dirent_t e; if (!neelefs_read_dirent(i, &e)) return -1;
        // ensure null-termination
        e.name[31] = '\0';
        if (str_eq(e.name, name)) { if (out) *out = e; return (int)i; }
    }
    return -1;
}

bool neelefs_cat(const char* name) {
    if (!g_mounted) { console_writeln("NeeleFS not mounted"); return false; }
    neelefs_dirent_t e; if (find_entry(name, &e) < 0) { console_writeln("Not found"); return false; }
    uint32_t remaining = e.size;
    uint32_t start = e.offset;
    uint8_t sec[512];
    while (remaining > 0) {
        uint32_t abs = start + (e.size - remaining);
        uint32_t lba = g_mount_lba + (abs / 512);
        uint32_t off = abs % 512;
        if (!ata_read_lba28(lba, 1, sec)) return false;
        uint32_t chunk = 512 - off; if (chunk > remaining) chunk = remaining;
        for (uint32_t i=0; i<chunk; i++) {
            uint8_t c = sec[off+i]; if (c < 32 || c > 126) c = '.'; char s[2]; s[0]=(char)c; s[1]=0; console_write(s);
        }
        remaining -= chunk;
    }
    console_write("\n");
    return true;
}
