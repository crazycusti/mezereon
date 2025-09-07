#include "shell.h"
#include "keyboard.h"
#include "drivers/ata.h"
#include "drivers/fs/neelefs.h"
#include "main.h"
#include "config.h"
#include "netface.h"
#include "cpu.h"
#include <stdint.h>

static void print_prompt(void) {
    console_write("mez> ");
}

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

void shell_run(void) {
    char buf[128];
    int len = 0;
    print_prompt();

    for (;;) {
        int ch = keyboard_poll_char();
        if (ch < 0) { netface_poll(); continue; }

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
                } else if (streq(buf, "help")) {
                    console_write("Commands: version, clear, help, cpuinfo, ata, atadump [lba], neele mount [lba], neele ls [path], neele cat <name|/path>, neele mkfs, neele mkdir </path>, neele write </path> <text>, pad </path>, netinfo, netrxdump\n");
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
                    // subcommands: mount [lba], ls [path], cat <name|path>, mkfs, mkdir <path>, write <path> <text>
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
                    } else {
                        console_write("usage: neele <mount|ls|cat> ...\n");
                    }
                } else if (streq(buf, "cpuinfo")) {
                    cpuinfo_print();
                } else if (streq(buf, "netinfo")) {
                    netface_diag_print();
                } else if (streq(buf, "netrxdump")) {
                    console_write("netrxdump: press 'q' to quit.\n");
                    for (;;) {
                        int k = keyboard_poll_char();
                        if (k == 'q' || k == 'Q') { console_write("\n"); break; }
                        netface_poll_rx();
                    }
                } else if (buf[0]=='p' && buf[1]=='a' && buf[2]=='d' && (buf[3]==' ' || buf[3]==0)) {
                    int i=3; while (buf[i]==' ') i++;
                    if (!buf[i]) { console_write("usage: pad </path>\n"); }
                    else {
                        // simple line-based editor
                        static char textbuf[4096]; uint32_t len=0; int modified=0;
                        // try to load existing
                        if (!neelefs_read_text(buf+i, textbuf, sizeof(textbuf)-1, &len)) { len=0; textbuf[0]=0; }
                        console_clear();
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
