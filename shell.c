#include "shell.h"
#include "keyboard.h"
#include "drivers/ata.h"
#include "main.h"
#include "config.h"
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
        if (ch < 0) continue;

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
                    video_init();
                } else if (streq(buf, "help")) {
                    video_print("Commands: version, clear, help, ata, atadump [lba]\n");
                } else if (streq(buf, "ata")) {
                    if (ata_present()) video_print("ATA present (primary master).\n");
                    else video_print("ATA not present.\n");
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
