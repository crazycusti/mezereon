#include "shell.h"
#include "keyboard.h"
#include "drivers/ata.h"
#include "drivers/fs/neelefs.h"
#include "drivers/storage.h"
#include "main.h"
#include "config.h"
#include "netface.h"
#include "cpu.h"
#include "cpuidle.h"
#include "interrupts.h"
#include "net/ipv4.h"
#include "drivers/pcspeaker.h"
#include "drivers/gpu/gpu.h"
#include "apps/fbtest_color.h"
#include "apps/gpu_probe.h"
#include "video_fb.h"
#include "mezapi.h"
#include "memory.h"
#include <stdint.h>

static void print_prompt(void) {
    console_write("mez> ");
}

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static void shell_write_u64_hex(uint64_t v) {
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)v;
    if (hi) {
        console_write_hex32(hi);
        console_write_hex32(lo);
    } else {
        console_write_hex32(lo);
    }
}

static uint32_t shell_clamp_u64_to_u32(uint64_t v) {
    return (v > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)v;
}

void shell_run(void) {
    char buf[128];
    int len = 0;
    print_prompt();

    for (;;) {
        video_cursor_tick();
        int ch = keyboard_poll_char();
        if (ch < 0) { netface_poll(); cpuidle_idle(); continue; }

        if (ch == '\r') ch = '\n';
        if (ch == '\n') {
            console_write("\n");
            buf[len] = 0;
            if (len > 0) {
                if (streq(buf, "version")) {
                    console_write("Mezereon ");
                    console_write(CONFIG_KERNEL_VERSION);
                    console_write("\n");
                } else if (streq(buf, "clear")) {
                    console_clear();
                } else if (streq(buf, "kbdump")) {
                    keyboard_debug_dump();
                } else if (streq(buf, "help")) {
                    console_write("Commands: version, clear, help, cpuinfo, meminfo, ticks, wakeups, idle [n], timer <show|hz N|off|on>, ata, atadump [lba], autofs [show|rescan|mount <n>], ip [show|set <ip> <mask> [gw]|ping <ip> [count]], neele mount [lba], neele ls [path], neele cat <name|/path>, neele mkfs, neele mkdir </path>, neele write </path> <text>, neele verify [verbose] [path], pad </path>, netinfo, netrxdump, gpuprobe [scan|noscan] [auto|noauto] [activate [400|480]], gpuinfo, fbtest, beep [freq] [ms], keymusic, rotcube, app [ls|run </path|name>], http [start [port]|stop|status|body <text>]\n");
                } else if (streq(buf, "ata")) {
                    if (ata_present()) console_write("ATA present (selected device).\n");
                    else console_write("ATA not present.\n");
                } else if (buf[0]=='a' && buf[1]=='t' && buf[2]=='a' && buf[3]==' ' && buf[4]=='s' && buf[5]=='c' && buf[6]=='a' && buf[7]=='n') {
                    ata_dev_t devs[4]; ata_scan(devs);
                    const char* names[4] = {"PM","PS","SM","SS"};
                    for (int i=0;i<4;i++){
                        console_write(names[i]); console_write(": io="); console_write_hex16(devs[i].io);
                        console_write(" ctrl="); console_write_hex16(devs[i].ctrl);
                        console_write(" drv="); console_write(devs[i].slave?"S":"M");
                        console_write(" type=");
                        switch (devs[i].type){
                            case ATA_NONE: console_write("none"); break;
                            case ATA_ATA: console_write("ata"); break;
                            case ATA_ATAPI: console_write("atapi"); break;
                        }
                        console_write("\n");
                    }
                } else if (buf[0]=='a' && buf[1]=='t' && buf[2]=='a' && buf[3]==' ' && buf[4]=='u' && buf[5]=='s' && buf[6]=='e') {
                    int i=7; while (buf[i]==' ') i++;
                    int idx = -1;
                    if (buf[i]>='0'&&buf[i]<='3') { idx = buf[i]-'0'; }
                    if (idx<0){ console_write("usage: ata use <0..3> (0=PM,1=PS,2=SM,3=SS)\n"); }
                    else {
                        uint16_t io = (idx<2)? (uint16_t)CONFIG_ATA_PRIMARY_IO : 0x170;
                        uint16_t ctrl = (idx<2)? (uint16_t)CONFIG_ATA_PRIMARY_CTRL : 0x376;
                        bool slave = (idx%2)==1;
                        ata_set_target(io, ctrl, slave);
                        ata_type_t t = ata_detect();
                        console_write("selected ");
                        console_write((idx==0)?"PM":(idx==1)?"PS":(idx==2)?"SM":"SS");
                        console_write(": ");
                        console_write((t==ATA_ATA)?"ata":(t==ATA_ATAPI)?"atapi":"none");
                        console_write("\n");
                    }
                } else if (buf[0]=='a' && buf[1]=='t' && buf[2]=='a' && buf[3]=='d' && buf[4]=='u' && buf[5]=='m' && buf[6]=='p') {
                    // parse optional LBA
                    uint32_t lba = 0;
                    int i=7;
                    while (buf[i]==' ') i++;
                    if (buf[i]) {
                        // simple decimal parse
                        uint32_t v=0; int any=0;
                        while (buf[i]>='0' && buf[i]<='9') { v = v*10 + (buf[i]-'0'); i++; any=1; }
                        if (any) lba = v;
                    }
                    if (!ata_init()) {
                        console_write("ATA init failed.\n");
                    } else {
                        ata_dump_lba(lba, 4); // up to 2KiB
                    }
                } else if (buf[0]=='n' && buf[1]=='e' && buf[2]=='e' && buf[3]=='l' && buf[4]=='e') {
                    // subcommands: mount [lba], ls [path], cat <name|path>, mkfs, mkdir <path>, write <path> <text>, verify [verbose] [path]
                    int i=5; while (buf[i]==' ') i++;
                    if (buf[i]==0) { console_write("usage: neele <mount|ls|cat> ...\n"); }
                    else if (buf[i]=='m' && buf[i+1]=='o') {
                        i+=5; while (buf[i]==' ') i++;
                        uint32_t lba = CONFIG_NEELEFS_LBA;
                        if (buf[i]) { uint32_t v=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){v=v*10+(buf[i]-'0');i++;any=1;} if (any) lba=v; }
                        if (!ata_present()) { console_write("ATA not present.\n"); }
                        else if (neelefs_mount(lba)) { console_write("NeeleFS mount OK.\n"); }
                        else { console_write("NeeleFS mount failed.\n"); }
                    } else if (buf[i]=='l' && buf[i+1]=='s') {
                        // optional path
                        i+=2; while (buf[i]==' ') i++;
                        if (buf[i]) { if (!neelefs_ls_path(buf+i)) console_write("ls failed.\n"); }
                        else neelefs_list();
                    } else if (buf[i]=='c' && buf[i+1]=='a' && buf[i+2]=='t') {
                        i+=3; while (buf[i]==' ') i++;
                        if (!buf[i]) { console_write("usage: neele cat <name|/path>\n"); }
                        else { if (!neelefs_cat(buf+i)) if (!neelefs_cat_path(buf+i)) console_write("cat failed.\n"); }
                    } else if (buf[i]=='m' && buf[i+1]=='k' && buf[i+2]=='f' && buf[i+3]=='s') {
                        // format auto up to 16MB at CONFIG_NEELEFS_LBA; optional "force"
                        uint32_t lba = CONFIG_NEELEFS_LBA; int force=0;
                        i+=4; while (buf[i]==' ') i++;
                        if (buf[i]){
                            // accept optional "force"
                            if (buf[i]=='f') { force=1; }
                        }
                        if (!ata_present()) { console_write("ATA not present.\n"); }
                        else if (force ? neelefs_mkfs_16mb_force(lba) : neelefs_mkfs_16mb(lba)) { console_write("mkfs OK.\n"); }
                        else { console_write("mkfs failed.\n"); }
                    } else if (buf[i]=='m' && buf[i+1]=='k' && buf[i+2]=='d' && buf[i+3]=='i' && buf[i+4]=='r') {
                        i+=5; while (buf[i]==' ') i++;
                        if (!buf[i]) { console_write("usage: neele mkdir </path>\n"); }
                        else { if (!neelefs_mkdir(buf+i)) console_write("mkdir failed.\n"); }
                    } else if (buf[i]=='w' && buf[i+1]=='r' && buf[i+2]=='i' && buf[i+3]=='t' && buf[i+4]=='e') {
                        i+=5; while (buf[i]==' ') i++;
                        if (!buf[i]) { console_write("usage: neele write </path> <text>\n"); }
                        else {
                            // path then space then text
                            char path[128]; int j=0; while (buf[i] && buf[i]!=' ' && j<127){ path[j++]=buf[i++]; } path[j]=0; while (buf[i]==' ') i++;
                            if (!buf[i]) { console_write("usage: neele write </path> <text>\n"); }
                            else { if (!neelefs_write_text(path, buf+i)) console_write("write failed.\n"); }
                        }
                    } else if (buf[i]=='v' && buf[i+1]=='e' && buf[i+2]=='r' && buf[i+3]=='i' && buf[i+4]=='f' && buf[i+5]=='y') {
                        i+=6; while (buf[i]==' ') i++;
                        int verbose=0;
                        // optional literal "verbose"
                        if (buf[i]=='v') { const char* kw="verbose"; int k=0; int ok=1; while (kw[k]){ if (buf[i+k]!=kw[k]){ ok=0; break; } k++; }
                            if (ok) { i+=7; while (buf[i]==' ') i++; verbose=1; }
                        }
                        if (buf[i]) { if (!neelefs_verify(buf+i, verbose)) console_write("verify failed.\n"); }
                        else { if (!neelefs_verify("/", verbose)) console_write("verify failed.\n"); }
                    } else {
                        console_write("usage: neele <mount|ls|cat> ...\n");
                    }
                } else if (streq(buf, "meminfo")) {
                    uint64_t total_kib = memory_total_bytes() >> 10;
                    uint64_t usable_kib = memory_usable_bytes() >> 10;
                    uint64_t alloc_kib = memory_allocated_bytes() >> 10;
                    console_write("mem: total=");
                    console_write_dec(shell_clamp_u64_to_u32(total_kib));
                    console_write(" KiB usable=");
                    console_write_dec(shell_clamp_u64_to_u32(usable_kib));
                    console_write(" KiB allocated=");
                    console_write_dec(shell_clamp_u64_to_u32(alloc_kib));
                    console_write(" KiB\n");
                    size_t regions = memory_region_count();
                    for (size_t ri = 0; ri < regions; ri++) {
                        const bootinfo_memory_range_t* r = memory_region_at(ri);
                        if (!r) {
                            continue;
                        }
                        console_write(" ");
                        console_write_dec((uint32_t)ri);
                        console_write(": base=0x");
                        shell_write_u64_hex(r->base);
                        console_write(" len=0x");
                        shell_write_u64_hex(r->length);
                        console_write(" (");
                        console_write_dec(shell_clamp_u64_to_u32(r->length >> 10));
                        console_write(" KiB)");
                        console_write(" type=");
                        console_write_dec(r->type);
                        if (r->attr) {
                            console_write(" attr=0x");
                            console_write_hex32(r->attr);
                        }
                        console_write("\n");
                    }
                } else if (streq(buf, "cpuinfo")) {
                    cpuinfo_print();
                } else if (streq(buf, "ticks")) {
                    console_write("ticks="); console_write_dec(ticks_get()); console_write("\n");
                } else if (streq(buf, "wakeups")) {
                    console_write("wakeups="); console_write_dec(cpuidle_wakeups_get()); console_write("\n");
                } else if (buf[0]=='i' && buf[1]=='d' && buf[2]=='l' && buf[3]=='e' && (buf[4]==0 || buf[4]==' ')) {
                    // idle [n] — perform HLT once or n times
                    int i=4; while (buf[i]==' ') i++;
                    if (!buf[i]) { cpuidle_idle(); console_writeln("(hlt)"); }
                    else {
                        uint32_t n=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){ n=n*10+(buf[i]-'0'); i++; any=1; }
                        if (!any || n==0) { console_writeln("usage: idle [n]"); }
                        else { for (uint32_t k=0;k<n;k++){ cpuidle_idle(); } console_writeln("(hlt xN)"); }
                    }
                } else if (streq(buf, "netinfo")) {
                    netface_diag_print();
                } else if (streq(buf, "netrxdump")) {
                    console_write("netrxdump: press 'q' to quit.\n");
                    for (;;) {
                        int k = keyboard_poll_char();
                        if (k == 'q' || k == 'Q') { console_write("\n"); break; }
                        netface_poll_rx();
                        cpuidle_idle();
                    }
                } else if (buf[0]=='a' && buf[1]=='u' && buf[2]=='t' && buf[3]=='o' && buf[4]=='f' && buf[5]=='s' && (buf[6]==' ' || buf[6]==0)) {
                    int i=6; while (buf[i]==' ') i++;
                    if (!buf[i] || (buf[i]=='s')) { // show default or 'show'
                        console_writeln("AutoFS devices (0..3):");
                        for (int k=0;k<storage_count();k++){
                            storage_info_t inf; if (!storage_get(k,&inf)) continue;
                            console_write(" "); console_write_dec((uint32_t)k); console_write(": ");
                            const char* slot = (inf.dev.io==CONFIG_ATA_PRIMARY_IO && !inf.dev.slave)?"PM":
                                               (inf.dev.io==CONFIG_ATA_PRIMARY_IO &&  inf.dev.slave)?"PS":
                                               (inf.dev.io==0x170 && !inf.dev.slave)?"SM":"SS";
                            console_write(slot);
                            if (!inf.present) { console_write("  (none)\n"); continue; }
                            console_write("  ATA  ");
                            if (inf.neelefs_found){ console_write("NeeleFS"); console_write(inf.neelefs_ver==2?"2":"1"); console_write(" @"); console_write_dec(inf.neelefs_lba); }
                            else console_write("no-fs");
                            if (inf.mounted) console_write("  [mounted]");
                            console_write("\n");
                        }
                    } else if (buf[i]=='r') { // rescan
                        storage_scan(); int m = storage_automount();
                        if (m>=0) { console_write("mounted index "); console_write_dec((uint32_t)m); console_write("\n"); }
                        else console_writeln("no mountable volumes");
                    } else if (buf[i]=='m') { // mount <idx>
                        i+=5; while (buf[i]==' ') i++;
                        uint32_t n=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){ n=n*10+(buf[i]-'0'); i++; any=1; }
                        if (!any) { console_writeln("usage: autofs mount <index>"); }
                        else { if (!storage_mount_index((int)n)) console_writeln("mount failed"); else console_writeln("mounted"); }
                    } else { console_writeln("usage: autofs [show|rescan|mount <n>]"); }
                } else if (buf[0]=='i' && buf[1]=='p' && (buf[2]==0 || buf[2]==' ')) {
                    int i=2; while (buf[i]==' ') i++;
                    if (!buf[i]) { net_ipv4_print_config(); }
                    else if (buf[i]=='s' && buf[i+1]=='e' && buf[i+2]=='t') {
                        i+=3; while (buf[i]==' ') i++;
                        // ip set <addr> <mask> [gw]
                        char a[16]={0}, m[16]={0}, g[16]={0}; int j=0;
                        while (buf[i] && buf[i]!=' ' && j<15){ a[j++]=buf[i++]; }
                        while (buf[i]==' ') i++;
                        j=0; while (buf[i] && buf[i]!=' ' && j<15){ m[j++]=buf[i++]; }
                        while (buf[i]==' ') i++;
                        j=0; while (buf[i] && buf[i]!=' ' && j<15){ g[j++]=buf[i++]; }
                        const char* gp = (g[0]?g:0);
                        if (!net_ipv4_set_from_strings(a,m,gp)) console_writeln("ip: bad address/mask/gw");
                        else { net_ipv4_print_config(); console_status_set_left("ip: set"); }
                    } else if (buf[i]=='p' && buf[i+1]=='i' && buf[i+2]=='n' && buf[i+3]=='g') {
                        // ip ping <addr> [count]
                        i+=4; while (buf[i]==' ') i++;
                        char a[16]={0}; int j=0; while (buf[i] && buf[i]!=' ' && j<15){ a[j++]=buf[i++]; }
                        while (buf[i]==' ') i++;
                        uint32_t cnt=1; while (buf[i]>='0'&&buf[i]<='9'){ cnt=cnt*10+(uint32_t)(buf[i]-'0'); i++; }
                        // parse dotted quad
                        int okp=0; uint32_t ipbe=0; {
                            const char* s=a; uint32_t acc=0; int part=0; uint32_t v=0; int d=0; okp=0;
                            while (*s && part<4){ if (*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); if (v>255) { okp=0; break; } d=1; }
                                else if (*s=='.'){ if (!d){ okp=0; break; } acc=(acc<<8)|v; v=0; d=0; part++; }
                                else { okp=0; break; } s++; }
                            if (part==3 && d){ acc=(acc<<8)|v; ipbe = ((acc&0xFF)<<24)|((acc&0xFF00)<<8)|((acc&0xFF0000)>>8)|((acc>>24)&0xFF); okp=1; }
                        }
                        if (!okp) console_writeln("ip ping: bad address");
                        else { net_icmp_ping(ipbe, (int)cnt, 1000); }
                    } else { console_writeln("usage: ip [show|set <ip> <mask> [gw]|ping <ip> [count]]"); }
                } else if (buf[0]=='t' && buf[1]=='i' && buf[2]=='m' && buf[3]=='e' && buf[4]=='r' && (buf[5]==' ' || buf[5]==0)) {
                    int i=5; while (buf[i]==' ') i++;
                    if (!buf[i] || (buf[i]=='s')) { // show
                        console_write("timer: hz="); console_write_dec(platform_timer_get_hz()); console_write("\n");
                    } else if (buf[i]=='h') { // hz N
                        i+=2; while (buf[i]==' ') i++;
                        uint32_t n=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){ n=n*10+(buf[i]-'0'); i++; any=1; }
                        if (!any || n==0) { console_writeln("usage: timer hz <n>"); }
                        else { platform_timer_set_hz(n); console_write("timer: hz="); console_write_dec(n); console_write("\n"); }
                    } else if (buf[i]=='o' && buf[i+1]=='f' && buf[i+2]=='f') { // off
                        platform_irq_set_mask(0,1); console_writeln("timer: off (IRQ0 masked)");
                    } else if (buf[i]=='o' && buf[i+1]=='n') { // on
                        platform_irq_set_mask(0,0); console_writeln("timer: on (IRQ0 unmasked)");
                    } else { console_writeln("usage: timer <show|hz N|off|on>"); }
                } else if (buf[0]=='b' && buf[1]=='e' && buf[2]=='e' && buf[3]=='p' && (buf[4]==0 || buf[4]==' ')) {
                    // beep [freq] [ms]
                    int i=4; while (buf[i]==' ') i++;
                    uint32_t f=880, ms=100; int anyf=0, anyms=0;
                    while (buf[i]>='0'&&buf[i]<='9'){ f=f*10+(buf[i]-'0'); i++; anyf=1; }
                    while (buf[i]==' ') i++;
                    while (buf[i]>='0'&&buf[i]<='9'){ ms=ms*10+(buf[i]-'0'); i++; anyms=1; }
                    if (!pcspeaker_present()) console_writeln("beep: speaker not present");
                    else { pcspeaker_beep(anyf?f:880, anyms?ms:100); console_writeln("(beep)"); }
                } else if (streq(buf, "keymusic")) {
                    extern int keymusic_app_main(const mez_api32_t*);
                    (void)keymusic_app_main(mez_api_get());
                } else if (streq(buf, "rotcube")) {
                    extern int rotcube_app_main(const mez_api32_t*);
                    (void)rotcube_app_main(mez_api_get());
                } else if (streq(buf, "fbtest")) {
                    fbtest_run();
                } else if (buf[0]=='g' && buf[1]=='p' && buf[2]=='u' && buf[3]=='p' && buf[4]=='r' && buf[5]=='o' && buf[6]=='b' && buf[7]=='e' && (buf[8]==0 || buf[8]==' ')) {
                    int i=8; while (buf[i]==' ') i++;
                    gpu_probe_run(buf[i] ? buf + i : NULL);
                } else if (buf[0]=='g' && buf[1]=='p' && buf[2]=='u' && buf[3]=='i' && buf[4]=='n' && buf[5]=='f' && buf[6]=='o' && (buf[7]==0 || buf[7]==' ')) {
                    int i=7; while (buf[i]==' ') i++;
                    if (!buf[i]) {
                        gpu_log_summary();
                    } else if (buf[i]=='d' && buf[i+1]=='e' && buf[i+2]=='t' && buf[i+3]=='a' && buf[i+4]=='i' && buf[i+5]=='l' && (buf[i+6]==0 || buf[i+6]==' ')) {
                        gpu_dump_details();
                    } else {
                        console_writeln("usage: gpuinfo [detail]");
                    }
                } else if (buf[0]=='a' && buf[1]=='p' && buf[2]=='p' && (buf[3]==0 || buf[3]==' ')) {
                    int i=3; while (buf[i]==' ') i++;
                    if (!buf[i] || (buf[i]=='l' && buf[i+1]=='s')) {
                        // app ls → list /apps (NeeleFS v2)
                        if (!neelefs_ls_path("/apps")) console_writeln("app: list failed (mount NeeleFS v2)");
                    } else if (buf[i]=='r' && buf[i+1]=='u' && buf[i+2]=='n') {
                        i+=3; while (buf[i]==' ') i++;
                        if (!buf[i]) { console_writeln("usage: app run </path|name>"); }
                        else {
                            // If '/': treat as file path; else: name
                            char name[64]; name[0]=0;
                            if (buf[i]=='/') {
                                // Read file and parse first token as name (ASCII). CRC verified by neelefs_read_text.
                                static char fbuf[1024]; uint32_t out_len=0;
                                if (!neelefs_read_text(buf+i, fbuf, sizeof(fbuf)-1, &out_len)) { console_writeln("app: read failed"); }
                                else {
                                    fbuf[out_len]=0; // NUL-terminate
                                    // Accept formats: "MEZCMD\n<name>" or just "<name>"
                                    const char* p=fbuf;
                                    if (p[0]=='M'&&p[1]=='E'&&p[2]=='Z'&&p[3]=='C'&&p[4]=='M'&&p[5]=='D'){ while (*p && *p!='\n') p++; if (*p=='\n') p++; }
                                    int j=0; while (*p && *p!='\n' && j<63){ name[j++]=*p++; } name[j]=0;
                                }
                            } else {
                                int j=0; while (buf[i] && buf[i]!=' ' && j<63){ name[j++]=buf[i++]; } name[j]=0;
                            }
                            if (name[0]==0){ console_writeln("app: no name"); }
                            else if (name[0]=='k'&&name[1]=='e'&&name[2]=='y'&&name[3]=='m'&&name[4]=='u'&&name[5]=='s'&&name[6]=='i'&&name[7]=='c'&&name[8]==0){
                                extern int keymusic_app_main(const mez_api32_t*);
                                (void)keymusic_app_main(mez_api_get());
                            } else if (name[0]=='r'&&name[1]=='o'&&name[2]=='t'&&name[3]=='c'&&name[4]=='u'&&name[5]=='b'&&name[6]=='e'&&name[7]==0){
                                extern int rotcube_app_main(const mez_api32_t*);
                                (void)rotcube_app_main(mez_api_get());
                            } else {
                                console_writeln("app: unknown name");
                            }
                        }
                    } else { console_writeln("usage: app [ls|run </path|name>]"); }
                } else if (buf[0]=='h' && buf[1]=='t' && buf[2]=='t' && buf[3]=='p' && (buf[4]==0 || buf[4]==' ')) {
                    extern void net_tcp_min_listen(uint16_t port);
                    extern void net_tcp_min_stop(void);
                    extern void net_tcp_min_status(void);
                    extern void net_tcp_min_set_http_body(const char* body);
                    extern void net_tcp_min_set_file_path(const char* path);
                    extern void net_tcp_min_use_inline(void);
                    int i=4; while (buf[i]==' ') i++;
                    if (!buf[i] || (buf[i]=='s' && buf[i+1]=='t' && buf[i+2]=='a' && buf[i+3]=='t' && buf[i+4]=='u' && buf[i+5]=='s')) {
                        net_tcp_min_status();
                    } else if (buf[i]=='s' && buf[i+1]=='t' && buf[i+2]=='a' && buf[i+3]=='r' && buf[i+4]=='t') {
                        i+=5; while (buf[i]==' ') i++;
                        // Parse optional port; default to 80 when omitted
                        uint32_t p=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){ p=p*10+(buf[i]-'0'); i++; any=1; }
                        if (any && p>0 && p<65536) net_tcp_min_listen((uint16_t)p); else net_tcp_min_listen(80);
                        console_writeln("http: listening");
                    } else if (buf[i]=='s' && buf[i+1]=='t' && buf[i+2]=='o' && buf[i+3]=='p') {
                        net_tcp_min_stop(); console_writeln("http: stopped");
                    } else if (buf[i]=='b' && buf[i+1]=='o' && buf[i+2]=='d' && buf[i+3]=='y') {
                        i+=4; while (buf[i]==' ') i++;
                        if (!buf[i]) console_writeln("usage: http body <text>"); else { net_tcp_min_set_http_body(buf+i); console_writeln("http: body set"); }
                    } else if (buf[i]=='f' && buf[i+1]=='i' && buf[i+2]=='l' && buf[i+3]=='e') {
                        i+=4; while (buf[i]==' ') i++;
                        if (!buf[i]) console_writeln("usage: http file </path>"); else { net_tcp_min_set_file_path(buf+i); console_writeln("http: file mode"); }
                    } else if (buf[i]=='i' && buf[i+1]=='n' && buf[i+2]=='l' && buf[i+3]=='i' && buf[i+4]=='n' && buf[i+5]=='e') {
                        net_tcp_min_use_inline(); console_writeln("http: inline mode");
                    } else { console_writeln("usage: http [start [port]|stop|status|body <text>]"); }
                } else if (buf[0]=='p' && buf[1]=='a' && buf[2]=='d' && (buf[3]==' ' || buf[3]==0)) {
                    int i=3; while (buf[i]==' ') i++;
                    if (!buf[i]) { console_write("usage: pad </path>\n"); }
                    else {
                        // simple line-based editor
                        static char textbuf[4096]; uint32_t len=0; int modified=0;
                        // try to load existing
                        int had_crc_error = 0;
                        if (!neelefs_read_text(buf+i, textbuf, sizeof(textbuf)-1, &len)) {
                            had_crc_error = 1; len=0; textbuf[0]=0;
                        }
                        console_clear();
                        // update status bar after clear
                        if (had_crc_error) console_status_set_left("pad: checksum mismatch"); else console_status_set_left("");
                        console_write("pad: "); console_write(buf+i); console_write("\n^S save  ^Q quit  (max 4K)\n\n");
                        // print current content
                        for (uint32_t k=0;k<len;k++){ char s[2]; s[0]=textbuf[k]?textbuf[k]:'.'; s[1]=0; console_write(s); }
                        for(;;){
                            int ch = keyboard_poll_char(); if (ch<0) { netface_poll(); continue; }
                            if (ch == 0x13) { // Ctrl+S
                                textbuf[len]=0; if (neelefs_write_text(buf+i, textbuf)) { console_write("\n[saved]\n"); modified=0; }
                                else { console_write("\n[save failed]\n"); }
                                continue;
                            }
                            if (ch == 0x11) { // Ctrl+Q
                                if (modified) console_write("\n[quit - changes may be lost]\n");
                                break;
                            }
                            if (ch == '\b') { if (len>0){ len--; console_putc('\b'); modified=1; } continue; }
                            if (ch == '\r') ch='\n';
                            if ((ch>=32 && ch<=126) || ch=='\n'){
                                if (len < sizeof(textbuf)-1){ textbuf[len++] = (char)ch; console_putc((char)ch); modified=1; }
                            }
                        }
                    }
                } else {
                    console_write("Unknown command: ");
                    console_write(buf);
                    console_write("\n");
                }
            }
            len = 0;
            print_prompt();
            continue;
        }

        if (ch == '\b') {
            if (len > 0) {
                len--;
                console_putc('\b');
            }
            continue;
        }

        if (ch >= 32 && ch <= 126) {
            if (len < (int)sizeof(buf)-1) {
                buf[len++] = (char)ch;
                console_putc((char)ch);
            }
        }
    }
}
