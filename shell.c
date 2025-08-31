#include "shell.h"
#include "keyboard.h"
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
                    video_print("Commands: version, clear, help\n");
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
