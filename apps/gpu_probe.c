#include "gpu_probe.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include <stddef.h>

static int token_equals(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int parse_chip_token(const char* token, gpu_type_t* out_type) {
    if (!token || !out_type) return 0;
    if (token_equals(token, "et4000")) {
        *out_type = GPU_TYPE_ET4000;
        return 1;
    }
    if (token_equals(token, "et4000ax")) {
        *out_type = GPU_TYPE_ET4000AX;
        return 1;
    }
    if (token_equals(token, "avga2")) {
        *out_type = GPU_TYPE_AVGA2;
        return 1;
    }
    if (token_equals(token, "cirrus") || token_equals(token, "cirrus-gd5446")) {
        *out_type = GPU_TYPE_CIRRUS;
        return 1;
    }
    if (token_equals(token, "vga")) {
        *out_type = GPU_TYPE_VGA;
        return 1;
    }
    return 0;
}

static int parse_decimal_segment(const char** cursor, unsigned long* out, unsigned long max_value) {
    if (!cursor || !*cursor || !out) return 0;
    const char* p = *cursor;
    if (*p < '0' || *p > '9') return 0;
    unsigned long value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10u + (unsigned long)(*p - '0');
        if (value > max_value) return 0;
        p++;
    }
    if (p == *cursor) return 0;
    *out = value;
    *cursor = p;
    return 1;
}

static int parse_mode_token(const char* token, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    if (!token || !width || !height || !bpp) return 0;
    const char* cursor = token;
    unsigned long w = 0, h = 0, bits = 0;
    if (!parse_decimal_segment(&cursor, &w, 0xFFFFu)) return 0;
    if (*cursor != 'x' && *cursor != 'X') return 0;
    cursor++;
    if (!parse_decimal_segment(&cursor, &h, 0xFFFFu)) return 0;
    if (*cursor != 'x' && *cursor != 'X') return 0;
    cursor++;
    if (!parse_decimal_segment(&cursor, &bits, 0xFFu)) return 0;
    if (*cursor != '\0') return 0;
    *width = (uint16_t)w;
    *height = (uint16_t)h;
    *bpp = (uint8_t)bits;
    return 1;
}

static int parse_legacy_height(const char* token, uint16_t* height) {
    if (!token || !height) return 0;
    unsigned long value = 0;
    const char* cursor = token;
    if (!parse_decimal_segment(&cursor, &value, 0xFFFFu)) return 0;
    if (*cursor != '\0') return 0;
    if (value == 400ul || value == 480ul) {
        *height = (uint16_t)value;
        return 1;
    }
    return 0;
}

static int parse_legacy_bpp_token(const char* token, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    if (!token || !width || !height || !bpp) return 0;
    if (!token_equals(token, "4bpp")) {
        return 0;
    }
    *width = 640;
    *height = 480;
    *bpp = 4;
    return 1;
}

static int gpuprobe_type_matches(gpu_type_t requested, gpu_type_t actual) {
    if (requested == actual) {
        return 1;
    }
    if ((requested == GPU_TYPE_ET4000 || requested == GPU_TYPE_ET4000AX || requested == GPU_TYPE_AVGA2) &&
        (actual == GPU_TYPE_ET4000 || actual == GPU_TYPE_ET4000AX || actual == GPU_TYPE_AVGA2)) {
        return 1;
    }
    return 0;
}

static int gpuprobe_type_is_tseng(gpu_type_t type) {
    return (type == GPU_TYPE_ET4000 || type == GPU_TYPE_ET4000AX || type == GPU_TYPE_AVGA2);
}

static const gpu_info_t* gpuprobe_find_device(const gpu_info_t* devices, size_t count, gpu_type_t requested_type) {
    if (!devices || count == 0) return NULL;
    for (size_t i = 0; i < count; ++i) {
        if (gpuprobe_type_matches(requested_type, devices[i].type)) {
            return &devices[i];
        }
    }
    return NULL;
}

static const char* gpuprobe_chip_token(gpu_type_t type) {
    switch (type) {
        case GPU_TYPE_ET4000: return "et4000";
        case GPU_TYPE_ET4000AX: return "et4000ax";
        case GPU_TYPE_AVGA2: return "avga2";
        case GPU_TYPE_CIRRUS: return "cirrus gd5446";
        case GPU_TYPE_VGA: return "vga";
        default: return "et4000";
    }
}

static void gpuprobe_print_mode_catalog(gpu_type_t requested_type) {
    size_t device_count = 0;
    const gpu_info_t* devices = gpu_get_devices(&device_count);
    const gpu_info_t* match = NULL;
    for (size_t i = 0; i < device_count; ++i) {
        const gpu_info_t* dev = &devices[i];
        if (gpuprobe_type_matches(requested_type, dev->type)) {
            match = dev;
            break;
        }
    }

    if (!match) {
        console_writeln("gpuprobe: no matching GPU found for mode listing");
        return;
    }

    gpu_mode_option_t options[16];
    size_t total = gpu_get_mode_catalog(match, options, sizeof(options) / sizeof(options[0]));
    if (total == 0) {
        console_writeln("gpuprobe: no framebuffer modes available for this adapter");
        return;
    }

    const char* name = match->name[0] ? match->name : "GPU";
    console_write("gpuprobe: available modes for ");
    console_write(name);
    console_writeln(":");
    size_t listed = (total > (sizeof(options) / sizeof(options[0]))) ? (sizeof(options) / sizeof(options[0])) : total;
    for (size_t i = 0; i < listed; ++i) {
        console_write("  - ");
        console_write_dec(options[i].width);
        console_write("x");
        console_write_dec(options[i].height);
        console_write("x");
        console_write_dec(options[i].bpp);
        console_writeln(" bpp");
    }
    if (listed < total) {
        console_writeln("  (additional modes omitted)");
    }
    console_write("  -> activate using 'gpuprobe activate ");
    console_write(gpuprobe_chip_token(requested_type));
    console_writeln(" <width>x<height>x<bpp>'");
}

void gpu_probe_run(const char* args) {
    int scan_requested = 0;
    int toggle_auto = -1; // -1=no change
    int status_requested = 0;
    int debug_toggle = -2; // -2=no change, 0=off,1=on
    int manual_requested = 0;
    int manual_ready = 0;
    gpu_type_t manual_type = GPU_TYPE_ET4000;
    uint16_t manual_width = 0;
    uint16_t manual_height = 0;
    uint8_t manual_bpp = 0;

    int expect_chip = 0;
    int expect_mode = 0;
    int expect_debug = 0;

    const char* p = args;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char token[32];
        int len = 0;
        while (*p && *p != ' ' && len < (int)(sizeof(token) - 1)) {
            token[len++] = *p++;
        }
        while (*p && *p != ' ') p++;
        token[len] = '\0';
        if (len == 0) continue;

        if (expect_debug) {
            if (token_equals(token, "on")) {
                debug_toggle = 1;
            } else if (token_equals(token, "off")) {
                debug_toggle = 0;
            } else {
                console_write("gpuprobe: expected 'on' or 'off' after 'debug', got '");
                console_write(token);
                console_writeln("'");
            }
            expect_debug = 0;
            continue;
        }

        if (expect_chip) {
            gpu_type_t parsed_type;
            if (parse_chip_token(token, &parsed_type)) {
                manual_type = parsed_type;
                manual_requested = 1;
                expect_chip = 0;
                if (manual_type == GPU_TYPE_VGA) {
                    manual_ready = 1;
                    manual_width = 0;
                    manual_height = 0;
                    manual_bpp = 0;
                } else {
                    expect_mode = 1;
                }
                continue;
            }
            // legacy fallback: treat next token as mode or height
            uint16_t legacy_height = 0;
            if (parse_mode_token(token, &manual_width, &manual_height, &manual_bpp)) {
                manual_requested = 1;
                manual_ready = 1;
                expect_chip = 0;
                expect_mode = 0;
                continue;
            } else if (parse_legacy_height(token, &legacy_height)) {
                manual_requested = 1;
                manual_ready = 1;
                manual_width = 640;
                manual_height = legacy_height;
                manual_bpp = 8;
                expect_chip = 0;
                expect_mode = 0;
                continue;
            } else {
                console_write("gpuprobe: unknown chip token '");
                console_write(token);
                console_writeln("'");
                expect_chip = 0;
                expect_mode = 0;
                manual_requested = 0;
                continue;
            }
        }

        if (expect_mode) {
            if (manual_type == GPU_TYPE_CIRRUS && token_equals(token, "gd5446")) {
                continue;
            }
            uint16_t w = 0, h = 0;
            uint8_t bits = 0;
            if (parse_mode_token(token, &w, &h, &bits)) {
                manual_width = w;
                manual_height = h;
                manual_bpp = bits;
                manual_ready = 1;
            } else if (parse_legacy_height(token, &h)) {
                manual_width = 640;
                manual_height = h;
                manual_bpp = 8;
                manual_ready = 1;
            } else if (parse_legacy_bpp_token(token, &w, &h, &bits)) {
                manual_width = w;
                manual_height = h;
                manual_bpp = bits;
                manual_ready = 1;
            } else {
                console_write("gpuprobe: invalid mode token '");
                console_write(token);
                console_writeln("'");
                manual_ready = 0;
            }
            expect_mode = 0;
            continue;
        }

        if (token[0] == '\0') {
            continue;
        } else if (token_equals(token, "noscan")) {
            scan_requested = 0;
        } else if (token_equals(token, "scan")) {
            scan_requested = 1;
        } else if (token_equals(token, "activate")) {
            manual_requested = 1;
            manual_ready = 0;
            manual_type = GPU_TYPE_ET4000;
            manual_width = manual_height = 0;
            manual_bpp = 0;
            expect_chip = 1;
        } else if (token_equals(token, "auto")) {
            toggle_auto = 1;
        } else if (token_equals(token, "noauto")) {
            toggle_auto = 0;
        } else if (token_equals(token, "debug")) {
            expect_debug = 1;
        } else if (token_equals(token, "status")) {
            status_requested = 1;
        } else {
            console_write("gpuprobe: unknown token '");
            console_write(token);
            console_writeln("' (use scan|noscan|activate <chip> <wxhxbpp>|auto|noauto|debug on/off|status)");
        }
    }

    if (expect_chip) {
        console_writeln("gpuprobe: expected chip after 'activate'");
        manual_requested = 0;
    }
    if (expect_mode) {
        console_writeln("gpuprobe: expected mode token after chip specification");
        manual_ready = 0;
    }
    if (expect_debug) {
        console_writeln("gpuprobe: expected 'on' or 'off' after 'debug'");
        expect_debug = 0;
    }

    if (debug_toggle != -2) {
        gpu_set_debug(debug_toggle);
    }

    size_t device_count = 0;
    const gpu_info_t* devices = gpu_get_devices(&device_count);
    const gpu_info_t* manual_device = NULL;
    if (manual_requested && manual_type != GPU_TYPE_VGA) {
        manual_device = gpuprobe_find_device(devices, device_count, manual_type);
    }

    int run_legacy_scan = scan_requested;
    if (!run_legacy_scan && manual_requested && manual_type != GPU_TYPE_VGA && !manual_device &&
        gpuprobe_type_is_tseng(manual_type)) {
        run_legacy_scan = 1;
    }

    console_writeln("gpuprobe: starting diagnostics");
    if (!run_legacy_scan) {
        console_writeln("gpuprobe: legacy Tseng scan skipped (use 'scan' to force)");
    } else if (!scan_requested) {
        console_writeln("gpuprobe: forcing legacy Tseng scan for requested activation");
    }
    gpu_debug_probe(run_legacy_scan);

    if (manual_requested && manual_type != GPU_TYPE_VGA) {
        devices = gpu_get_devices(&device_count);
        manual_device = gpuprobe_find_device(devices, device_count, manual_type);
        if (!manual_device) {
            console_write("gpuprobe: no detected ");
            console_write(gpuprobe_chip_token(manual_type));
            if (gpuprobe_type_is_tseng(manual_type)) {
                console_writeln(" adapter (activation skipped, try 'gpuprobe scan')");
            } else {
                console_writeln(" adapter (activation skipped)");
            }
            manual_ready = 0;
        } else {
            gpuprobe_print_mode_catalog(manual_type);
        }
    }

    if (toggle_auto != -1) {
        gpu_tseng_set_auto_enabled(toggle_auto);
    }

    if (manual_requested) {
        if (!manual_ready) {
            console_writeln("gpuprobe: activation aborted (missing or invalid mode)");
        } else {
            const char* chip_name = gpuprobe_chip_token(manual_type);
            console_write("gpuprobe: activating ");
            console_write(chip_name);
            if (manual_width && manual_height) {
                console_write(" ");
                console_write_dec(manual_width);
                console_write("x");
                console_write_dec(manual_height);
                console_write("x");
                console_write_dec(manual_bpp);
            }
            console_writeln("");
            if (!gpu_force_activate(manual_type, manual_width, manual_height, manual_bpp)) {

                console_write("gpuprobe: activation failed: ");
                console_writeln(gpu_get_last_error());
            } else {
                char name[16];
                uint16_t w = 0, h = 0;
                uint8_t bits = 0;
                gpu_get_last_mode(name, sizeof(name), &w, &h, &bits);
                console_write("gpuprobe: activation successful -> ");
                console_write(name);
                if (w && h) {
                    console_write(" ");
                    console_write_dec(w);
                    console_write("x");
                    console_write_dec(h);
                    console_write("x");
                    console_write_dec(bits);
                }
                console_writeln("");
            }
        }
    }

    if (status_requested) {
        gpu_print_status();
    }
}
