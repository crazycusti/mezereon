#include "gpu_probe.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"

void gpu_probe_run(const char* args) {
    int scan = 1;
    int activate = 0;
    uint16_t activate_height = 480;
    int expect_height = 0;
    int toggle_auto = -1; // -1=no change, 0=disable, 1=enable

    const char* p = args;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char token[16];
        int len = 0;
        while (*p && *p != ' ' && len < (int)(sizeof(token) - 1)) {
            token[len++] = *p++;
        }
        while (*p && *p != ' ') p++;
        token[len] = '\0';
        if (len == 0) continue;

        if (expect_height) {
            expect_height = 0;
            uint16_t h = 0;
            for (int i = 0; token[i]; i++) {
                if (token[i] < '0' || token[i] > '9') { h = 0; break; }
                h = (uint16_t)(h * 10u + (uint16_t)(token[i] - '0'));
            }
            if (h == 400 || h == 480) {
                activate_height = h;
            } else {
                console_write("gpuprobe: ignoring unsupported height token '");
                console_write(token);
                console_writeln("'");
            }
            continue;
        }

        if (token[0] == '\0') {
            continue;
        } else if (token[0] == 'n' && token[1] == 'o' && token[2] == 's' && token[3] == 'c' && token[4] == 'a' && token[5] == 'n' && token[6] == '\0') {
            scan = 0;
        } else if (token[0] == 's' && token[1] == 'c' && token[2] == 'a' && token[3] == 'n' && token[4] == '\0') {
            scan = 1;
        } else if (token[0] == 'a' && token[1] == 'c' && token[2] == 't' && token[3] == 'i' && token[4] == 'v' && token[5] == 'a' && token[6] == 't' && token[7] == 'e' && token[8] == '\0') {
            activate = 1;
            expect_height = 1;
        } else if (token[0] == 'a' && token[1] == 'u' && token[2] == 't' && token[3] == 'o' && token[4] == '\0') {
            toggle_auto = 1;
        } else if (token[0] == 'n' && token[1] == 'o' && token[2] == 'a' && token[3] == 'u' && token[4] == 't' && token[5] == 'o' && token[6] == '\0') {
            toggle_auto = 0;
        } else {
            console_write("gpuprobe: unknown token '");
            console_write(token);
            console_writeln("' (use scan|noscan|activate [400|480]|auto|noauto)");
        }
    }

    console_writeln("gpuprobe: starting diagnostics");
    if (!scan) {
        console_writeln("gpuprobe: legacy scan disabled (pass 'scan' to force)");
    }
    gpu_debug_probe(scan);

    if (toggle_auto != -1) {
        gpu_tseng_set_auto_enabled(toggle_auto);
    }

    if (activate) {
        console_write("gpuprobe: activating ET4000 ");
        console_write_dec((uint32_t)activate_height);
        console_writeln("p framebuffer");
        if (!gpu_manual_activate_et4000(640, activate_height, 8)) {
            console_writeln("gpuprobe: activation failed");
        }
    }
}
