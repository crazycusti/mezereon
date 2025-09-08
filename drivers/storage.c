#include "storage.h"
#include "../console.h"
#include "fs/neelefs.h"

static storage_info_t g_infos[4];
static int g_count = 0;

static int detect_neele_magic(uint32_t lba, int* out_ver){
    uint8_t sec[512];
    if (!ata_read_lba28(lba,1,sec)) return 0;
    const char* m = (const char*)sec;
    // v2 preferred
    const char* m2 = "NEELEFS2";
    int is2 = 1; for (int i=0;i<8;i++){ if (m[i]!=m2[i]){ is2=0; break; } }
    if (is2){ if (out_ver) *out_ver=2; return 1; }
    const char* m1 = "NEELEFS1";
    int is1 = 1; for (int i=0;i<8;i++){ if (m[i]!=m1[i]){ is1=0; break; } }
    if (is1){ if (out_ver) *out_ver=1; return 1; }
    return 0;
}

void storage_scan(void){
    ata_dev_t devs[4]; ata_scan(devs);
    g_count = 4;
    for (int i=0;i<4;i++){
        g_infos[i].dev = devs[i];
        g_infos[i].present = (devs[i].type == ATA_ATA) ? 1 : 0;
        g_infos[i].neelefs_found = 0;
        g_infos[i].neelefs_ver = 0;
        g_infos[i].neelefs_lba = 0;
        g_infos[i].mounted = 0;
        if (!g_infos[i].present) continue;
        // Select device for probing
        ata_set_target(devs[i].io, devs[i].ctrl, devs[i].slave);
        int ver=0;
        if (detect_neele_magic(2048, &ver)){
            g_infos[i].neelefs_found = 1; g_infos[i].neelefs_ver = ver; g_infos[i].neelefs_lba = 2048;
        } else if (detect_neele_magic(0, &ver)){
            g_infos[i].neelefs_found = 1; g_infos[i].neelefs_ver = ver; g_infos[i].neelefs_lba = 0;
        }
    }
}

int storage_count(void){ return g_count; }

int storage_get(int idx, storage_info_t* out){
    if (idx < 0 || idx >= g_count) return 0;
    if (out) *out = g_infos[idx];
    return 1;
}

static int cmp_rank(int ver, uint32_t lba){
    // Prefer v2 over v1 and LBA 2048 over 0
    int rank = 0;
    if (ver == 2) rank += 2; else if (ver == 1) rank += 1;
    if (lba == 2048u) rank += 1;
    return rank;
}

int storage_mount_index(int idx){
    if (idx < 0 || idx >= g_count) return 0;
    storage_info_t* inf = &g_infos[idx];
    if (!inf->present || !inf->neelefs_found) return 0;
    // Select device and mount NeeleFS at detected LBA
    ata_set_target(inf->dev.io, inf->dev.ctrl, inf->dev.slave);
    if (neelefs_mount(inf->neelefs_lba)){
        // clear previous mounted marks
        for (int i=0;i<g_count;i++) g_infos[i].mounted = 0;
        inf->mounted = 1;
        return 1;
    }
    return 0;
}

int storage_automount(void){
    int best_idx = -1; int best_rank = -1;
    for (int i=0;i<g_count;i++){
        if (!g_infos[i].present || !g_infos[i].neelefs_found) continue;
        int r = cmp_rank(g_infos[i].neelefs_ver, g_infos[i].neelefs_lba);
        if (r > best_rank){ best_rank = r; best_idx = i; }
    }
    if (best_idx >= 0){ if (storage_mount_index(best_idx)) return best_idx; }
    return -1;
}

