#include "gpu_dump.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include "../drivers/gpu/et4000.h"
#include "../mezapi.h"
#include "../config.h"

#include <stdint.h>
#include <stddef.h>

typedef enum {
    GPUDUMP_TARGET_AUTO = 0,
    GPUDUMP_TARGET_ALL,
    GPUDUMP_TARGET_SPECIFIC,
} gpudump_target_t;

static int token_equals(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static int read_token(const char** cursor, char* out, size_t out_len) {
    if (!cursor || !out || out_len == 0) {
        return 0;
    }
    const char* p = *cursor;
    if (!p) {
        out[0] = '\0';
        return 0;
    }
    while (*p == ' ') {
        ++p;
    }
    if (*p == '\0') {
        *cursor = p;
        out[0] = '\0';
        return 0;
    }
    size_t len = 0;
    while (*p && *p != ' ' && len + 1 < out_len) {
        out[len++] = *p++;
    }
    out[len] = '\0';
    while (*p == ' ') {
        ++p;
    }
    *cursor = p;
    return len > 0;
}

static int parse_u32(const char* text, uint32_t* out) {
    if (!text || !*text || !out) {
        return 0;
    }
    uint32_t value = 0;
    int base = 10;
    const char* p = text;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
        if (!*p) {
            return 0;
        }
    }
    while (*p) {
        char c = *p++;
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            return 0;
        }
        value = value * (uint32_t)base + (uint32_t)digit;
    }
    *out = value;
    return 1;
}

static int token_is_numeric(const char* token) {
    if (!token || !*token) {
        return 0;
    }
    const char* p = token;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!*p) {
            return 0;
        }
        while (*p) {
            char c = *p++;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return 0;
            }
        }
        return 1;
    }
    while (*p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int parse_chip_token(const char* token, gpu_type_t* out_type) {
    if (!token || !out_type) {
        return 0;
    }
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

static int type_matches(gpu_type_t requested, gpu_type_t actual) {
    if (requested == actual) {
        return 1;
    }
    if ((requested == GPU_TYPE_ET4000 || requested == GPU_TYPE_ET4000AX || requested == GPU_TYPE_AVGA2) &&
        (actual == GPU_TYPE_ET4000 || actual == GPU_TYPE_ET4000AX || actual == GPU_TYPE_AVGA2)) {
        return 1;
    }
    return 0;
}

static const gpu_info_t* find_device(const gpu_info_t* devices, size_t count, gpu_type_t requested) {
    if (!devices || count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (type_matches(requested, devices[i].type)) {
            return &devices[i];
        }
    }
    return NULL;
}

static int map_adapter_to_type(uint32_t adapter, gpu_type_t* out_type) {
    if (!out_type) {
        return 0;
    }
    switch (adapter) {
        case MEZ_GPU_ADAPTER_CIRRUS:
            *out_type = GPU_TYPE_CIRRUS;
            return 1;
        case MEZ_GPU_ADAPTER_TSENG_ET4000:
            *out_type = GPU_TYPE_ET4000;
            return 1;
        case MEZ_GPU_ADAPTER_TSENG_ET4000AX:
            *out_type = GPU_TYPE_ET4000AX;
            return 1;
        case MEZ_GPU_ADAPTER_ACUMOS_AVGA2:
            *out_type = GPU_TYPE_AVGA2;
            return 1;
        default:
            return 0;
    }
}

static const gpu_info_t* select_auto_device(const gpu_info_t* devices, size_t count) {
    const mez_api32_t* api = mez_api_get();
    if (api && (api->capabilities & MEZ_CAP_VIDEO_GPU_INFO) && api->video_gpu_get_info) {
        const mez_gpu_info32_t* info = api->video_gpu_get_info();
        if (info) {
            gpu_type_t preferred;
            if (map_adapter_to_type(info->adapter_type, &preferred)) {
                const gpu_info_t* match = find_device(devices, count, preferred);
                if (match) {
                    return match;
                }
            }
        }
    }
    return (count > 0) ? &devices[0] : NULL;
}

static const char* type_label(gpu_type_t type) {
    switch (type) {
        case GPU_TYPE_CIRRUS: return "cirrus gd5446";
        case GPU_TYPE_ET4000: return "et4000";
        case GPU_TYPE_ET4000AX: return "et4000ax";
        case GPU_TYPE_AVGA2: return "avga2";
        case GPU_TYPE_VGA: return "vga";
        default: return "unknown";
    }
}

static void print_header(const gpu_info_t* gpu, size_t index, size_t total) {
    const char* name = (gpu && gpu->name[0]) ? gpu->name : "GPU";
    console_write("gpudump: ");
    if (total > 1) {
        console_write("[");
        console_write_dec((uint32_t)index);
        console_write("/");
        console_write_dec((uint32_t)total);
        console_write("] ");
    }
    console_write(name);
    if (gpu && (gpu->pci.vendor_id || gpu->pci.device_id)) {
        console_write(" (vendor=");
        console_write_hex16(gpu->pci.vendor_id);
        console_write(", device=");
        console_write_hex16(gpu->pci.device_id);
        console_write(", bus=");
        console_write_dec((uint32_t)gpu->pci.bus);
        console_write(", dev=");
        console_write_dec((uint32_t)gpu->pci.device);
        console_write(", fn=");
        console_write_dec((uint32_t)gpu->pci.function);
        console_write(")");
    } else {
        console_write(" (legacy/ISA)");
    }
    console_writeln("");
}

static void run_regs(gpudump_target_t target, gpu_type_t requested_type) {
    size_t count = 0;
    const gpu_info_t* devices = gpu_get_devices(&count);
    if (count == 0) {
        console_writeln("gpudump: no supported GPU adapters detected.");
        return;
    }

    if (target == GPUDUMP_TARGET_ALL) {
        for (size_t i = 0; i < count; ++i) {
            const gpu_info_t* dev = &devices[i];
            print_header(dev, i + 1, count);
            gpu_dump_registers(dev);
            if (i + 1 < count) {
                console_writeln("");
            }
        }
        return;
    }

    const gpu_info_t* selected = NULL;
    if (target == GPUDUMP_TARGET_SPECIFIC) {
        selected = find_device(devices, count, requested_type);
        if (!selected) {
            console_write("gpudump: no adapter matching '");
            console_write(type_label(requested_type));
            console_writeln("' detected.");
            return;
        }
    } else {
        selected = select_auto_device(devices, count);
        if (!selected) {
            selected = &devices[0];
        }
    }

    print_header(selected, 1, 1);
    gpu_dump_registers(selected);
}

static void command_regs(const char** cursor) {
    char token[32];
    if (!read_token(cursor, token, sizeof(token))) {
        run_regs(GPUDUMP_TARGET_AUTO, GPU_TYPE_UNKNOWN);
        return;
    }

    if (token_equals(token, "all")) {
        run_regs(GPUDUMP_TARGET_ALL, GPU_TYPE_UNKNOWN);
        return;
    }
    if (token_equals(token, "auto")) {
        run_regs(GPUDUMP_TARGET_AUTO, GPU_TYPE_UNKNOWN);
        return;
    }

    gpu_type_t type;
    if (parse_chip_token(token, &type)) {
        if (type == GPU_TYPE_CIRRUS) {
            const char* skip = *cursor;
            char extra[32];
            if (read_token(&skip, extra, sizeof(extra))) {
                if (token_equals(extra, "gd5446")) {
                    *cursor = skip;
                } else {
                    console_write("gpudump: unexpected argument '");
                    console_write(extra);
                    console_writeln("'");
                    *cursor = skip;
                }
            }
        }
        run_regs(GPUDUMP_TARGET_SPECIFIC, type);
        return;
    }

    console_write("gpudump: unknown adapter token '");
    console_write(token);
    console_writeln("'");
}

static void handle_bank_command(int capture, const char* initial_token, const char** cursor) {
#if CONFIG_VIDEO_ENABLE_ET4000
    uint32_t bank = 0;
    uint32_t offset = 0;
    uint32_t length = 0x100u;
    char token[32];

    const char* bank_token = initial_token;
    if (!bank_token) {
        if (!read_token(cursor, token, sizeof(token))) {
            console_writeln(capture ? "usage: gpudump capture <bank> [offset] [len]"
                                   : "usage: gpudump bank <bank> [offset] [len]");
            return;
        }
        bank_token = token;
    }

    if (!parse_u32(bank_token, &bank)) {
        console_writeln("gpudump: invalid bank value");
        return;
    }

    if (read_token(cursor, token, sizeof(token))) {
        if (!parse_u32(token, &offset)) {
            console_writeln("gpudump: invalid offset");
            return;
        }
        if (read_token(cursor, token, sizeof(token))) {
            if (!parse_u32(token, &length)) {
                console_writeln("gpudump: invalid length");
                return;
            }
        }
    }

    if (bank > 0x0Fu) {
        console_writeln("gpudump: warning - bank index exceeds 0x0F");
    }

    if (capture) {
        if (!et4000_capture_dump((uint8_t)bank, offset, length)) {
            console_writeln("gpudump: capture failed");
        }
    } else {
        et4000_dump_bank((uint8_t)bank, offset, length);
    }
#else
    (void)capture;
    (void)initial_token;
    (void)cursor;
    console_writeln("gpudump: Tseng driver disabled at build time");
#endif
}

void gpu_dump_run(const char* args) {
    const char* cursor = args;
    char token[32];
    if (!read_token(&cursor, token, sizeof(token))) {
        run_regs(GPUDUMP_TARGET_AUTO, GPU_TYPE_UNKNOWN);
        return;
    }

    if (token_equals(token, "regs")) {
        command_regs(&cursor);
        return;
    }
    if (token_equals(token, "all")) {
        run_regs(GPUDUMP_TARGET_ALL, GPU_TYPE_UNKNOWN);
        return;
    }
    if (token_equals(token, "auto")) {
        run_regs(GPUDUMP_TARGET_AUTO, GPU_TYPE_UNKNOWN);
        return;
    }
    if (token_equals(token, "bank")) {
        handle_bank_command(0, NULL, &cursor);
        return;
    }
    if (token_equals(token, "capture")) {
        handle_bank_command(1, NULL, &cursor);
        return;
    }

    gpu_type_t type;
    if (parse_chip_token(token, &type)) {
        if (type == GPU_TYPE_CIRRUS) {
            const char* skip = cursor;
            char extra[32];
            if (read_token(&skip, extra, sizeof(extra))) {
                if (token_equals(extra, "gd5446")) {
                    cursor = skip;
                } else {
                    console_write("gpudump: unexpected argument '");
                    console_write(extra);
                    console_writeln("'");
                    cursor = skip;
                }
            }
        }
        run_regs(GPUDUMP_TARGET_SPECIFIC, type);
        return;
    }

    if (token_is_numeric(token)) {
        handle_bank_command(0, token, &cursor);
        return;
    }

    console_write("gpudump: unknown command '");
    console_write(token);
    console_writeln("'");
}
