#include "shell.h"
#include "keyboard.h"
#include "drivers/ata.h"
#include "drivers/fs/neelefs.h"
#include "main.h"
#include "config.h"
#include "network.h"
#include <stdint.h>

static void print_prompt(void) {
    video_print("mez> ");
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
        if (ch < 0) { network_poll(); continue; }

        if (ch == '\r') ch = '\n';
        if (ch == '\n') {
            video_print("\n");
            buf[len] = 0;
            if (len > 0) {
                if (streq(buf, "version")) {
                    video_print("Mezereon ");
                    video_print(CONFIG_KERNEL_VERSION);
                    video_print("\n");
                } else if (streq(buf, "clear")) {
                    video_clear();
                } else if (streq(buf, "help")) {
                    video_print("Commands: version, clear, help, ata, atadump [lba], neele mount [lba], neele ls, neele cat <name>, netrxdump\n");
                } else if (streq(buf, "ata")) {
                    if (ata_present()) video_print("ATA present (selected device).\n");
                    else video_print("ATA not present.\n");
                } else if (buf[0]=='a' && buf[1]=='t' && buf[2]=='a' && buf[3]==' ' && buf[4]=='s' && buf[5]=='c' && buf[6]=='a' && buf[7]=='n') {
                    ata_dev_t devs[4]; ata_scan(devs);
                    const char* names[4] = {"PM","PS","SM","SS"};
                    for (int i=0;i<4;i++){
                        video_print(names[i]); video_print(": io="); video_print_hex16(devs[i].io);
                        video_print(" ctrl="); video_print_hex16(devs[i].ctrl);
                        video_print(" drv="); video_print(devs[i].slave?"S":"M");
                        video_print(" type=");
                        switch (devs[i].type){
                            case ATA_NONE: video_print("none"); break;
                            case ATA_ATA: video_print("ata"); break;
                            case ATA_ATAPI: video_print("atapi"); break;
                        }
                        video_print("\n");
                    }
                } else if (buf[0]=='a' && buf[1]=='t' && buf[2]=='a' && buf[3]==' ' && buf[4]=='u' && buf[5]=='s' && buf[6]=='e') {
                    int i=7; while (buf[i]==' ') i++;
                    int idx = -1;
                    if (buf[i]>='0'&&buf[i]<='3') { idx = buf[i]-'0'; }
                    if (idx<0){ video_print("usage: ata use <0..3> (0=PM,1=PS,2=SM,3=SS)\n"); }
                    else {
                        uint16_t io = (idx<2)? (uint16_t)CONFIG_ATA_PRIMARY_IO : 0x170;
                        uint16_t ctrl = (idx<2)? (uint16_t)CONFIG_ATA_PRIMARY_CTRL : 0x376;
                        bool slave = (idx%2)==1;
                        ata_set_target(io, ctrl, slave);
                        ata_type_t t = ata_detect();
                        video_print("selected ");
                        video_print((idx==0)?"PM":(idx==1)?"PS":(idx==2)?"SM":"SS");
                        video_print(": ");
                        video_print((t==ATA_ATA)?"ata":(t==ATA_ATAPI)?"atapi":"none");
                        video_print("\n");
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
                        video_print("ATA init failed.\n");
                    } else {
                        ata_dump_lba(lba, 4); // up to 2KiB
                    }
                } else if (buf[0]=='n' && buf[1]=='e' && buf[2]=='e' && buf[3]=='l' && buf[4]=='e') {
                    // subcommands: mount [lba], ls, cat <name>
                    int i=5; while (buf[i]==' ') i++;
                    if (buf[i]==0) { video_print("usage: neele <mount|ls|cat> ...\n"); }
                    else if (buf[i]=='m' && buf[i+1]=='o') {
                        i+=5; while (buf[i]==' ') i++;
                        uint32_t lba = CONFIG_NEELEFS_LBA;
                        if (buf[i]) { uint32_t v=0; int any=0; while (buf[i]>='0'&&buf[i]<='9'){v=v*10+(buf[i]-'0');i++;any=1;} if (any) lba=v; }
                        if (!ata_present()) { video_print("ATA not present.\n"); }
                        else if (neelefs_mount(lba)) { video_print("NeeleFS mount OK.\n"); }
                        else { video_print("NeeleFS mount failed.\n"); }
                    } else if (buf[i]=='l' && buf[i+1]=='s') {
                        neelefs_list();
                    } else if (buf[i]=='c' && buf[i+1]=='a' && buf[i+2]=='t') {
                        i+=3; while (buf[i]==' ') i++;
                        if (!buf[i]) { video_print("usage: neele cat <name>\n"); }
                        else {
                            char name[33]; int j=0; while (buf[i] && buf[i]!=' ' && j<32) { name[j++]=buf[i++]; } name[j]='\0';
                            if (!neelefs_cat(name)) { video_print("cat failed.\n"); }
                        }
                    } else {
                        video_print("usage: neele <mount|ls|cat> ...\n");
                    }
                } else if (streq(buf, "netrxdump")) {
                    video_print("netrxdump: press 'q' to quit.\n");
                    for (;;) {
                        int k = keyboard_poll_char();
                        if (k == 'q' || k == 'Q') { video_print("\n"); break; }
                        ne2000_poll_rx();
                    }
                } else {
                    video_print("Unknown command: ");
                    video_print(buf);
                    video_print("\n");
                }
            }
            len = 0;
            print_prompt();
            continue;
        }

        if (ch == '\b') {
            if (len > 0) {
                len--;
                video_putc('\b');
            }
            continue;
        }

        if (ch >= 32 && ch <= 126) {
            if (len < (int)sizeof(buf)-1) {
                buf[len++] = (char)ch;
                video_putc((char)ch);
            }
        }
    }
}
