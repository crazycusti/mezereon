#include "ata.h"
#include "../config.h"
#include "../main.h"
#include "../keyboard.h"
#include <stdint.h>
#include "../arch/x86/io.h"

// ATA I/O ports
static uint16_t ATA_IO    = (uint16_t)CONFIG_ATA_PRIMARY_IO;
static uint16_t ATA_CTRL  = (uint16_t)CONFIG_ATA_PRIMARY_CTRL;
static bool     ATA_SLAVE = false; // false=master, true=slave

// Registers (offsets from ATA_IO)
#define ATA_REG_DATA      0
#define ATA_REG_ERROR     1
#define ATA_REG_FEATURES  1
#define ATA_REG_SECCNT    2
#define ATA_REG_LBA0      3
#define ATA_REG_LBA1      4
#define ATA_REG_LBA2      5
#define ATA_REG_DRIVE     6
#define ATA_REG_STATUS    7
#define ATA_REG_COMMAND   7

// Control block
#define ATA_REG_DEVCTRL   0   // at ATA_CTRL
#define ATA_REG_ALTSTATUS 0   // at ATA_CTRL

// Status bits
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DREQ 0x08
#define ATA_SR_ERR  0x01

// Commands
#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30

static void ata_400ns_delay(void){
    (void)inb(ATA_CTRL); (void)inb(ATA_CTRL); (void)inb(ATA_CTRL); (void)inb(ATA_CTRL);
}

static bool ata_wait_bsy_clear(void){
    for (int i=0;i<100000;i++){ if ((inb(ATA_IO+ATA_REG_STATUS) & ATA_SR_BSY)==0) return true; }
    return false;
}

static bool ata_wait_drq_set(void){
    for (int i=0;i<100000;i++){
        uint8_t st = inb(ATA_IO+ATA_REG_STATUS);
        if (st & ATA_SR_ERR) return false;
        if (st & ATA_SR_DREQ) return true;
    }
    return false;
}

void ata_set_target(uint16_t io, uint16_t ctrl, bool slave){
    ATA_IO = io; ATA_CTRL = ctrl; ATA_SLAVE = slave;
}

bool ata_init(void){
    if (!ata_present()) return false;
    // Disable IRQs (nIEN=1)
    outb(ATA_CTRL+ATA_REG_DEVCTRL, 0x02);
    if (!ata_wait_bsy_clear()) return false;

    // Select master, LBA mode high bits zero
    outb(ATA_IO+ATA_REG_DRIVE, (uint8_t)((ATA_SLAVE ? 0xF0 : 0xE0)));
    ata_400ns_delay();

    // IDENTIFY (final, to clear DRQ and sync)
    outb(ATA_IO+ATA_REG_SECCNT, 0);
    outb(ATA_IO+ATA_REG_LBA0, 0);
    outb(ATA_IO+ATA_REG_LBA1, 0);
    outb(ATA_IO+ATA_REG_LBA2, 0);
    outb(ATA_IO+ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // If status becomes 0, no device.
    if (inb(ATA_IO+ATA_REG_STATUS)==0) return false;

    if (!ata_wait_bsy_clear()) return false;

    uint8_t st = inb(ATA_IO+ATA_REG_STATUS);
    if (st & ATA_SR_ERR) return false;
    if (!ata_wait_drq_set()) return false;

    // Read identify data and drop it
    for (int i=0;i<256;i++){ (void)inw(ATA_IO+ATA_REG_DATA); }
    return true;
}

static ata_type_t g_ata_type = ATA_NONE;

ata_type_t ata_detect(void){
    // Select target (master/slave), CHS select; LBA not needed for IDENTIFY
    outb(ATA_IO+ATA_REG_DRIVE, (uint8_t)(ATA_SLAVE ? 0xB0 : 0xA0));
    ata_400ns_delay();

    uint8_t st = inb(ATA_IO+ATA_REG_STATUS);
    if (st == 0xFF || st == 0x00) { g_ata_type = ATA_NONE; return g_ata_type; }

    // Zero sector count/LBA regs and issue IDENTIFY
    outb(ATA_IO+ATA_REG_SECCNT, 0);
    outb(ATA_IO+ATA_REG_LBA0, 0);
    outb(ATA_IO+ATA_REG_LBA1, 0);
    outb(ATA_IO+ATA_REG_LBA2, 0);
    outb(ATA_IO+ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // If status becomes 0, no device
    st = inb(ATA_IO+ATA_REG_STATUS);
    if (st == 0) { g_ata_type = ATA_NONE; return g_ata_type; }

    // Wait while busy
    if (!ata_wait_bsy_clear()) { g_ata_type = ATA_NONE; return g_ata_type; }

    st = inb(ATA_IO+ATA_REG_STATUS);
    if (st & ATA_SR_ERR) {
        // Could be ATAPI: LBA1=0x14, LBA2=0xEB after IDENTIFY for packet
        uint8_t l1 = inb(ATA_IO+ATA_REG_LBA1);
        uint8_t l2 = inb(ATA_IO+ATA_REG_LBA2);
        if (l1 == 0x14 && l2 == 0xEB) { g_ata_type = ATA_ATAPI; }
        else { g_ata_type = ATA_NONE; }
        return g_ata_type;
    }
    if (st & ATA_SR_DREQ) { g_ata_type = ATA_ATA; return g_ata_type; }
    g_ata_type = ATA_NONE; return g_ata_type;
}

bool ata_present(void){
    if (g_ata_type == ATA_NONE) (void)ata_detect();
    return g_ata_type == ATA_ATA;
}

void ata_scan(ata_dev_t out[4]){
    const uint16_t ios[2] = { (uint16_t)CONFIG_ATA_PRIMARY_IO, 0x170 };
    const uint16_t ctrls[2] = { (uint16_t)CONFIG_ATA_PRIMARY_CTRL, 0x376 };
    for (int ch=0; ch<2; ch++){
        for (int sl=0; sl<2; sl++){
            int idx = ch*2 + sl;
            ata_set_target(ios[ch], ctrls[ch], sl==1);
            out[idx].io = ios[ch]; out[idx].ctrl = ctrls[ch]; out[idx].slave = (sl==1);
            out[idx].type = ata_detect();
        }
    }
}

bool ata_read_lba28(uint32_t lba, uint8_t sectors, void* buf){
    if (sectors==0 || sectors>4) sectors=4;
    if (!ata_wait_bsy_clear()) return false;

    outb(ATA_CTRL+ATA_REG_DEVCTRL, 0x02); // nIEN=1
    outb(ATA_IO+ATA_REG_DRIVE, (uint8_t)((ATA_SLAVE ? 0xF0 : 0xE0) | ((lba>>24)&0x0F)));
    outb(ATA_IO+ATA_REG_SECCNT, sectors);
    outb(ATA_IO+ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_IO+ATA_REG_LBA1, (uint8_t)((lba>>8)&0xFF));
    outb(ATA_IO+ATA_REG_LBA2, (uint8_t)((lba>>16)&0xFF));
    outb(ATA_IO+ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t* w = (uint16_t*)buf;
    for (uint8_t s=0; s<sectors; s++){
        if (!ata_wait_bsy_clear()) return false;
        if (!ata_wait_drq_set()) return false;
        for (int i=0;i<256;i++) *w++ = inw(ATA_IO+ATA_REG_DATA);
    }
    return true;
}

bool ata_write_lba28(uint32_t lba, uint8_t sectors, const void* buf){
    if (sectors==0 || sectors>4) sectors=4;
    if (!ata_wait_bsy_clear()) return false;

    outb(ATA_CTRL+ATA_REG_DEVCTRL, 0x02); // nIEN=1
    outb(ATA_IO+ATA_REG_DRIVE, (uint8_t)((ATA_SLAVE ? 0xF0 : 0xE0) | ((lba>>24)&0x0F)));
    outb(ATA_IO+ATA_REG_SECCNT, sectors);
    outb(ATA_IO+ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_IO+ATA_REG_LBA1, (uint8_t)((lba>>8)&0xFF));
    outb(ATA_IO+ATA_REG_LBA2, (uint8_t)((lba>>16)&0xFF));
    outb(ATA_IO+ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t* w = (const uint16_t*)buf;
    for (uint8_t s=0; s<sectors; s++){
        if (!ata_wait_bsy_clear()) return false;
        if (!ata_wait_drq_set()) return false;
        for (int i=0;i<256;i++) outw(ATA_IO+ATA_REG_DATA, *w++);
    }
    return true;
}

static void print_hex8(uint8_t v){
    char tmp[3];
    static const char H[] = "0123456789ABCDEF";
    tmp[0]=H[(v>>4)&0xF]; tmp[1]=H[v&0xF]; tmp[2]=0; console_write(tmp);
}

void ata_dump_lba(uint32_t lba, uint8_t sectors_max){
    if (sectors_max==0 || sectors_max>4) sectors_max=4;
    static uint8_t buf[2048];
    if (!ata_read_lba28(lba, sectors_max, buf)){
        console_write("ATA read failed.\n");
        return;
    }
    uint32_t total = (uint32_t)sectors_max * 512u;
    console_write("-- atadump: Down/Enter=next, PgDn=+16 lines, q=quit --\n");
    for (uint32_t off=0; off<total; off+=16){
        // address
        console_write_hex16((uint16_t)off);
        console_write(": ");
        // hex bytes
        for (uint32_t i=0;i<16;i++){
            print_hex8(buf[off+i]);
            console_write(" ");
        }
        // ascii
        console_write(" ");
        for (uint32_t i=0;i<16;i++){
            uint8_t c = buf[off+i];
            if (c<32 || c>126) c='.';
            char s[2]; s[0]=(char)c; s[1]=0; console_write(s);
        }
        console_write("\n");
        // Wait for navigation key: Down/Enter/Space = next line, PgDn = +16 lines, q = quit
        int advance_lines = 1;
        for (;;) {
            int ch = keyboard_poll_char();
            if (ch < 0) continue;
            if (ch == KEY_DOWN || ch == '\n' || ch == '\r' || ch == ' ') break;
            if (ch == KEY_PGDN) { advance_lines = 16; break; }
            if (ch == 'q' || ch == 'Q') { return; }
        }
        if (advance_lines > 1) {
            uint32_t add = (uint32_t)(advance_lines - 1) * 16u;
            if (total - off > add) {
                off += add;
            } else {
                // Reaching end; loop increment will terminate.
            }
        }
    }
}
