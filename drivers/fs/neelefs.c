// NeeleFS v1 (legacy) and v2 (write-enabled)
#include "neelefs.h"
#include "../../config.h"
#include "../../main.h"
#include "../../console.h"
#include "../ata.h"
#include <stdint.h>

#define NEELEFS_MAGIC_STR "NEELEFS1"
// New v2 magic
#define NEELEFS2_MAGIC_STR "NEELEFS2"

static uint32_t g_mount_lba = 0;
static int g_mounted = 0;
static uint32_t g_count = 0;
static uint32_t g_table_bytes = 0;

// v2 globals
static int g_is_v2 = 0;
static uint32_t g_v2_total_blocks = 0; // 16MB/512=32768
static uint32_t g_v2_bitmap_start = 0; // LBA offset from mount
static uint32_t g_v2_root_block = 0;   // block index of root dir

// v2 on-disk structures
typedef struct {
    char     magic[8];      // "NEELEFS2"
    uint32_t version;       // 2
    uint16_t block_size;    // 512
    uint16_t reserved0;
    uint32_t total_blocks;  // incl super
    uint32_t bitmap_start;  // block index where bitmap begins
    uint32_t root_block;    // block index of root directory
    uint32_t super_csum;    // simple crc32 of header with this field zero
    uint8_t  pad[512-8-4-2-2-4-4-4-4];
} __attribute__((packed)) ne2_super_t;

typedef struct {
    char     name[32];
    uint8_t  type;    // 1=file, 2=dir
    uint8_t  reserved[3];
    uint32_t first_block;
    uint32_t size_bytes;
    uint32_t csum;    // crc32 for files; 0 for dirs
    uint32_t mtime;   // optional
    uint8_t  pad[64-32-1-3-4-4-4-4];
} __attribute__((packed)) ne2_dirent_disk_t; // 64 bytes

#define NE2_DIRBLK_MAGIC 0x454E3244u /* 'D2NE' little-endian */
typedef struct {
    uint32_t magic;           // NE2_DIRBLK_MAGIC
    uint32_t next_block;      // 0 if none
    uint16_t entry_size;      // 64
    uint16_t entries_per_blk; // (512 - sizeof(hdr))/entry_size
    uint32_t reserved;
} __attribute__((packed)) ne2_dirblk_hdr_t; // 16 bytes

// crc32 (polynomial 0xEDB88320)
static uint32_t crc32_update(uint32_t crc, const uint8_t* p, uint32_t len){
    crc = ~crc;
    for (uint32_t i=0;i<len;i++){
        crc ^= p[i];
        for (int b=0;b<8;b++) crc = (crc>>1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
    return ~crc;
}

static void* memzero(void* dst, uint32_t n){ uint8_t* d=(uint8_t*)dst; while(n--) *d++=0; return dst; }
static void* memcpy_small(void* dst, const void* src, uint32_t n){ uint8_t* d=(uint8_t*)dst; const uint8_t* s=(const uint8_t*)src; while(n--) *d++=*s++; return dst; }

static inline uint32_t blocks_for_bytes(uint32_t sz){ return (sz + 512u - 1u) / 512u; }

// --- v1 (legacy) remains below as-is ---

static int str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

bool neelefs_mount(uint32_t lba) {
    // Read first sector
    uint8_t sec[512];
    if (!ata_read_lba28(lba, 1, sec)) { console_writeln("NeeleFS: read failed"); return false; }
    const char* m = (const char*)sec;
    // Detect v2 first
    int is_v2 = 1;
    for (int i=0;i<8;i++){ if (m[i]!=NEELEFS2_MAGIC_STR[i]) { is_v2=0; break; } }
    if (is_v2){
        ne2_super_t sb; memcpy_small(&sb, sec, 512);
        // minimal checks
        if (sb.block_size != 512 || sb.total_blocks == 0) { console_writeln("NeeleFS2: bad super"); return false; }
        g_is_v2 = 1; g_mount_lba = lba; g_mounted = 1;
        g_v2_total_blocks = sb.total_blocks;
        g_v2_bitmap_start = sb.bitmap_start;
        g_v2_root_block = sb.root_block;
        console_writeln("NeeleFS2 mounted");
        return true;
    }
    // Fallback to legacy v1
    for (int i=0; i<8; i++) {
        if (m[i] != NEELEFS_MAGIC_STR[i]) { console_writeln("NeeleFS: bad magic"); return false; }
    }
    uint32_t* p32 = (uint32_t*)(sec + 8);
    g_count = p32[0];
    g_table_bytes = p32[1];
    if (g_table_bytes == 0) g_table_bytes = g_count * sizeof(neelefs_dirent_t);
    g_table_bytes = align_up(g_table_bytes + 16, 512) - 16; // account header in sector 0
    g_mount_lba = lba;
    g_mounted = 1; g_is_v2 = 0;
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
    if (g_is_v2) { (void)neelefs_ls_path("/"); return; }
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
    if (g_is_v2) { return neelefs_cat_path(name); }
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

// ===== NeeleFS v2 implementation =====

static int path_next(const char** pp, char* out_name){
    const char* p = *pp; while (*p=='/') p++;
    int i=0; while (*p && *p!='/' && i<32){ out_name[i++] = *p++; }
    out_name[i]=0; *pp = p; return i;
}

static int bitmap_get(uint32_t idx){
    uint32_t byte = idx >> 3; uint8_t sec[512];
    uint32_t lba = g_mount_lba + g_v2_bitmap_start + (byte >> 9);
    uint16_t off = (uint16_t)(byte & 0x1FF);
    if (!ata_read_lba28(lba,1,sec)) return 1; // assume used on error
    return (sec[off] >> (idx & 7)) & 1;
}
static int bitmap_set(uint32_t idx, int used){
    uint32_t byte = idx >> 3; uint8_t sec[512];
    uint32_t lba = g_mount_lba + g_v2_bitmap_start + (byte >> 9);
    uint16_t off = (uint16_t)(byte & 0x1FF);
    if (!ata_read_lba28(lba,1,sec)) return 0;
    if (used) sec[off] |= (uint8_t)(1u << (idx & 7)); else sec[off] &= (uint8_t)~(1u << (idx & 7));
    return ata_write_lba28(lba,1,sec);
}
static uint32_t alloc_contig(uint32_t nblocks){
    // naive first-fit after bitmap start + 1 (skip super+bitmap region)
    uint32_t start = g_v2_bitmap_start + ( (g_v2_total_blocks + 4095)/4096 );
    uint32_t run=0, run_start=0;
    for (uint32_t i=start;i<g_v2_total_blocks;i++){
        if (!bitmap_get(i)){ if (run==0) run_start=i; run++; if (run>=nblocks){
                for (uint32_t b=0;b<nblocks;b++) bitmap_set(run_start+b,1);
                return run_start;
            }} else { run=0; }
    }
    return 0; // failure
}

static int dir_load_block(uint32_t block_idx, uint8_t* sec){
    return ata_read_lba28(g_mount_lba + block_idx, 1, sec) ? 1 : 0;
}
static int dir_store_block(uint32_t block_idx, const uint8_t* sec){
    return ata_write_lba28(g_mount_lba + block_idx, 1, sec) ? 1 : 0;
}

static int dir_init_block(uint32_t block_idx){
    uint8_t sec[512]; memzero(sec,512);
    ne2_dirblk_hdr_t* h = (ne2_dirblk_hdr_t*)sec;
    h->magic = NE2_DIRBLK_MAGIC;
    h->next_block = 0;
    h->entry_size = (uint16_t)sizeof(ne2_dirent_disk_t);
    h->entries_per_blk = (uint16_t)((512 - (uint32_t)sizeof(ne2_dirblk_hdr_t)) / (uint32_t)sizeof(ne2_dirent_disk_t));
    h->reserved = 0;
    return dir_store_block(block_idx, sec);
}

static int dir_scan_chain(uint32_t dir_block, uint32_t* last_blk_out){
    // Validate headers along chain; return last block in last_blk_out
    uint8_t sec[512]; uint32_t blk = dir_block; uint32_t last = dir_block;
    while (1){
        if (!dir_load_block(blk, sec)) return 0;
        ne2_dirblk_hdr_t* h = (ne2_dirblk_hdr_t*)sec;
        if (h->magic != NE2_DIRBLK_MAGIC) return 0;
        last = blk;
        if (h->next_block == 0) break;
        blk = h->next_block;
    }
    if (last_blk_out) *last_blk_out = last;
    return 1;
}

static int dir_find_entry(uint32_t dir_block, const char* name, ne2_dirent_disk_t* out, uint32_t* out_index){
    uint8_t sec[512]; uint32_t blk = dir_block;
    while (1){
        if (!dir_load_block(blk,sec)) return 0;
        ne2_dirblk_hdr_t* h = (ne2_dirblk_hdr_t*)sec;
        int hdr_ok = (h->magic == NE2_DIRBLK_MAGIC);
        int entries = hdr_ok ? h->entries_per_blk : (512 / (int)sizeof(ne2_dirent_disk_t));
        uint32_t base = hdr_ok ? (uint32_t)sizeof(ne2_dirblk_hdr_t) : 0u;
        ne2_dirent_disk_t* e = (ne2_dirent_disk_t*)(sec + base);
        for (int i=0;i<entries;i++){
            if (e[i].name[0]==0) { continue; }
            int ok=1; for(int j=0;j<32;j++){ char c=name[j]; if (c==0){ if (e[i].name[j]!=0) ok=0; break; } if (e[i].name[j]!=c) { ok=0; break; } }
            if (ok){ if (out) *out = e[i]; if (out_index) *out_index = (uint32_t)i; return 1; }
        }
        if (!hdr_ok || h->next_block==0) break;
        blk = h->next_block;
    }
    return 0;
}
static int dir_add_entry(uint32_t dir_block, const ne2_dirent_disk_t* ent){
    uint8_t sec[512]; uint32_t blk = dir_block; uint32_t last=dir_block;
    while (1){
        if (!dir_load_block(blk,sec)) return 0;
        last = blk;
        ne2_dirblk_hdr_t* h = (ne2_dirblk_hdr_t*)sec;
        int hdr_ok = (h->magic == NE2_DIRBLK_MAGIC);
        if (!hdr_ok){
            // initialize header in-place (upgrade)
            ne2_dirblk_hdr_t* nh = (ne2_dirblk_hdr_t*)sec;
            nh->magic=NE2_DIRBLK_MAGIC; nh->next_block=0; nh->entry_size=(uint16_t)sizeof(ne2_dirent_disk_t); nh->entries_per_blk=(uint16_t)((512 - (uint32_t)sizeof(ne2_dirblk_hdr_t))/sizeof(ne2_dirent_disk_t)); nh->reserved=0;
        }
        int entries = ((ne2_dirblk_hdr_t*)sec)->entries_per_blk;
        uint32_t base = (uint32_t)sizeof(ne2_dirblk_hdr_t);
        ne2_dirent_disk_t* e = (ne2_dirent_disk_t*)(sec + base);
        for (int i=0;i<entries;i++){
            if (e[i].name[0]==0){ e[i] = *ent; return dir_store_block(blk,sec); }
        }
        // go next or append new
        if (((ne2_dirblk_hdr_t*)sec)->next_block){ blk = ((ne2_dirblk_hdr_t*)sec)->next_block; continue; }
        // allocate new block and link
        uint32_t nb = alloc_contig(1); if (!nb) return 0;
        if (!dir_init_block(nb)) return 0;
        ((ne2_dirblk_hdr_t*)sec)->next_block = nb;
        if (!dir_store_block(blk,sec)) return 0;
        // write entry into new block
        if (!dir_load_block(nb,sec)) return 0;
        ne2_dirent_disk_t* e2 = (ne2_dirent_disk_t*)(sec + sizeof(ne2_dirblk_hdr_t));
        e2[0] = *ent;
        return dir_store_block(nb,sec);
    }
}

bool neelefs_mkfs_16mb(uint32_t lba){
    // 16MB region => 32768 blocks
    ne2_super_t sb; memzero(&sb, sizeof(sb));
    for (int i=0;i<8;i++) sb.magic[i]=NEELEFS2_MAGIC_STR[i];
    sb.version = 2; sb.block_size=512; sb.total_blocks = 32768u;
    // bitmap blocks: ceil(total_blocks/8 / 512) = ceil(32768/4096)=8 blocks
    sb.bitmap_start = 1; // blocks 1..8 used for bitmap
    sb.root_block = 9;   // first dir block
    // compute csum with field zero
    sb.super_csum = 0; sb.super_csum = crc32_update(0, (const uint8_t*)&sb, 512);

    // write superblock
    if (!ata_write_lba28(lba,1,&sb)) return false;
    // zero bitmap and dir block
    uint8_t zero[512]; memzero(zero,512);
    for (uint32_t i=0;i<8;i++) if (!ata_write_lba28(lba + 1 + i,1,zero)) return false;
    // init root dir block with header
    if (!dir_init_block(sb.root_block)) return false;
    // mark reserved blocks in bitmap: 0 (super), 1..8 bitmap, 9 root
    g_mount_lba = lba; g_is_v2=1; g_v2_total_blocks=sb.total_blocks; g_v2_bitmap_start=sb.bitmap_start; g_v2_root_block=sb.root_block; g_mounted=1;
    bitmap_set(0,1); for (uint32_t b=1;b<=8;b++) bitmap_set(b,1); bitmap_set(sb.root_block,1);
    console_writeln("NeeleFS2 formatted (16MB)");
    return true;
}

static int resolve_path(const char* path, uint32_t* dir_block_out, char* leaf){
    // Resolve parent dir for leaf; start at root
    uint32_t dirb = g_v2_root_block; const char* p=path; char name[33];
    int depth=0; for(;;){ int n = path_next(&p, name); if (n==0) break; depth++; if (depth>255) return 0; const char* next=p; if (*next=='/' && *(next+1)!=0){ // still more after this component
            ne2_dirent_disk_t e; if (!dir_find_entry(dirb,name,&e,0) || e.type!=2) return 0; dirb = e.first_block;
            while (*p=='/') p++;
        } else {
            // leaf
            for (int i=0;i<33;i++){ leaf[i]=0; }
            for (int i=0;i<n && i<32;i++) leaf[i]=name[i];
            *dir_block_out = dirb; return 1;
        }
    }
    // path was "/" — parent is root, leaf empty
    *dir_block_out = dirb; leaf[0]=0; return 1;
}

bool neelefs_ls_path(const char* path){
    if (!g_mounted || !g_is_v2) { console_writeln("NeeleFS2 not mounted"); return false; }
    uint32_t dirb; char leaf[33]; if (!resolve_path(path,&dirb,leaf)) { console_writeln("bad path"); return false; }
    if (leaf[0]){
        ne2_dirent_disk_t e; if (!dir_find_entry(dirb,leaf,&e,0)) { console_writeln("not found"); return false; }
        if (e.type==1){ console_write(" "); console_write(leaf); console_write(" "); console_write_dec(e.size_bytes); console_write(" bytes\n"); return true; }
        dirb = e.first_block;
    }
    uint8_t sec[512]; uint32_t blk=dirb;
    while (1){
        if (!dir_load_block(blk,sec)) return false;
        ne2_dirblk_hdr_t* h=(ne2_dirblk_hdr_t*)sec; int hdr_ok = (h->magic==NE2_DIRBLK_MAGIC);
        int entries = hdr_ok ? h->entries_per_blk : (512 / (int)sizeof(ne2_dirent_disk_t));
        uint32_t base = hdr_ok ? (uint32_t)sizeof(ne2_dirblk_hdr_t) : 0u;
        ne2_dirent_disk_t* ents=(ne2_dirent_disk_t*)(sec + base);
        for (int i=0;i<entries;i++){ if (ents[i].name[0]==0) continue; console_write(ents[i].type==2?"[D] ":"    "); console_write(ents[i].name); console_write(" "); console_write_dec(ents[i].size_bytes); console_write("\n"); }
        if (!hdr_ok || h->next_block==0) break;
        blk = h->next_block;
    }
    return true;
}

bool neelefs_mkdir(const char* path){
    if (!g_mounted || !g_is_v2) { console_writeln("NeeleFS2 not mounted"); return false; }
    uint32_t dirb; char leaf[33]; if (!resolve_path(path,&dirb,leaf) || !leaf[0]) { console_writeln("bad path"); return false; }
    ne2_dirent_disk_t e; uint32_t idx; if (dir_find_entry(dirb,leaf,&e,&idx)) { console_writeln("exists"); return false; }
    uint32_t b = alloc_contig(1); if (!b){ console_writeln("no space"); return false; }
    // init new dir block
    if (!dir_init_block(b)) return false;
    ne2_dirent_disk_t ne; memzero(&ne,sizeof(ne));
    for (int i=0;i<32;i++){ ne.name[i]= (leaf[i]?leaf[i]:0); if(!leaf[i]) break; }
    ne.type=2; ne.first_block=b; ne.size_bytes=0; ne.csum=0; ne.mtime=0;
    if (!dir_add_entry(dirb,&ne)) return false;
    console_writeln("dir created");
    return true;
}

bool neelefs_write_text(const char* path, const char* text){
    if (!g_mounted || !g_is_v2) { console_writeln("NeeleFS2 not mounted"); return false; }
    uint32_t dirb; char leaf[33]; if (!resolve_path(path,&dirb,leaf) || !leaf[0]) { console_writeln("bad path"); return false; }
    // measure text
    uint32_t len=0; while (text && text[len]) len++;
    uint32_t nb = blocks_for_bytes(len);
    uint32_t b = alloc_contig(nb?nb:1); if (!b){ console_writeln("no space"); return false; }
    // write content
    const uint8_t* p=(const uint8_t*)text; uint8_t sec[512];
    for (uint32_t i=0;i<nb;i++){
        uint32_t chunk = (len>512)?512:len;
        memzero(sec,512);
        if (chunk) memcpy_small(sec,p,chunk);
        if (!ata_write_lba28(g_mount_lba + b + i,1,sec)) return false;
        if (len>chunk){ len-=chunk; p+=chunk; } else len=0;
    }
    // add or replace entry
    ne2_dirent_disk_t cur; uint32_t idx;
    if (dir_find_entry(dirb,leaf,&cur,&idx)){
        // replace by writing updated entry
        uint8_t dirsec[512]; if (!dir_load_block(dirb,dirsec)) return false; ne2_dirent_disk_t* ents=(ne2_dirent_disk_t*)dirsec; ents[idx].first_block=b; ents[idx].size_bytes=(uint32_t)(len + (p?(uint32_t)0:0)); // we lost original len; recompute
        // recompute len properly
        uint32_t sl=0; while (text && text[sl]) sl++; ents[idx].size_bytes=sl; ents[idx].type=1; ents[idx].csum = crc32_update(0,(const uint8_t*)text,sl);
        return dir_store_block(dirb,dirsec);
    } else {
        ne2_dirent_disk_t ne; memzero(&ne,sizeof(ne)); for (int i=0;i<32;i++){ ne.name[i] = (leaf[i]?leaf[i]:0); if(!leaf[i]) break; }
        uint32_t sl=0; while (text && text[sl]) sl++;
        ne.type=1; ne.first_block=b; ne.size_bytes=sl; ne.csum=crc32_update(0,(const uint8_t*)text,sl);
        return dir_add_entry(dirb,&ne);
    }
}

bool neelefs_cat_path(const char* path){
    if (!g_mounted || !g_is_v2) { console_writeln("NeeleFS2 not mounted"); return false; }
    uint32_t dirb; char leaf[33]; if (!resolve_path(path,&dirb,leaf) || !leaf[0]) { console_writeln("bad path"); return false; }
    ne2_dirent_disk_t e; if (!dir_find_entry(dirb,leaf,&e,0) || e.type!=1) { console_writeln("not found"); return false; }
    uint32_t len = e.size_bytes; uint32_t nb = blocks_for_bytes(len); uint8_t sec[512]; uint32_t remain=len; uint32_t blk=e.first_block;
    for (uint32_t i=0;i<nb;i++){
        if (!ata_read_lba28(g_mount_lba + blk + i,1,sec)) return false;
        uint32_t chunk = (remain>512)?512:remain;
        for (uint32_t j=0;j<chunk;j++){
            uint8_t c=sec[j]; if (c<32||c>126) c='.'; char s[2]; s[0]=(char)c; s[1]=0; console_write(s);
        }
        remain -= chunk;
    }
    console_write("\n");
    return true;
}

bool neelefs_read_text(const char* path, char* out, uint32_t out_max, uint32_t* out_len){
    if (!g_mounted || !g_is_v2) { return false; }
    if (!out || out_max==0) return false;
    uint32_t dirb; char leaf[33]; if (!resolve_path(path,&dirb,leaf) || !leaf[0]) return false;
    ne2_dirent_disk_t e; if (!dir_find_entry(dirb,leaf,&e,0) || e.type!=1) return false;
    uint32_t remain = e.size_bytes; uint32_t blk = e.first_block; uint8_t sec[512]; uint32_t written=0;
    while (remain>0 && written<out_max){
        if (!ata_read_lba28(g_mount_lba + blk,1,sec)) break;
        uint32_t chunk = (remain>512)?512:remain;
        uint32_t copy = (written+chunk>out_max)?(out_max-written):chunk;
        if (copy>0) memcpy_small(out+written, sec, copy);
        written += copy; if (remain>chunk) remain -= chunk; else remain=0; blk++;
    }
    if (written < out_max) out[written]=0;
    if (out_len) *out_len = written;
    return true;
}
