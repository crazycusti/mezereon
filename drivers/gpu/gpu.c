#include "gpu.h"
#include "cirrus.h"
#include "../../config.h"
#include "et4000.h"
#include "avga2.h"
#include "../../console.h"
#include "../../display.h"
#include "../../video_fb.h"
#include "cirrus_accel.h"
#include "et4000_common.h"
#include <stdint.h>
#include <stddef.h>

#if defined(GPU_DEBUG) && GPU_DEBUG
#undef CONFIG_GPU_DEBUG
#define CONFIG_GPU_DEBUG 1
#endif

#ifndef CONFIG_GPU_DEBUG
#define CONFIG_GPU_DEBUG 0
#endif

static int g_gpu_debug = CONFIG_GPU_DEBUG ? 1 : 0;
static char g_gpu_last_error[128] = "OK";
static char g_gpu_last_mode_name[16] = "none";
static uint16_t g_gpu_last_mode_width = 0;
static uint16_t g_gpu_last_mode_height = 0;
static uint8_t g_gpu_last_mode_bpp = 0;

static void gpu_memzero(void* dst, size_t len) {
    uint8_t* p = (uint8_t*)dst;
    while (len--) {
        *p++ = 0;
    }
}

static void gpu_copy_string(char* dst, size_t len, const char* src) {
    if (!dst || len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void gpu_set_last_error(const char* msg) {
    gpu_copy_string(g_gpu_last_error, sizeof(g_gpu_last_error), msg ? msg : "unknown");
}

const char* gpu_get_last_error(void) {
    return g_gpu_last_error;
}

void gpu_set_last_mode(const char* name, uint16_t width, uint16_t height, uint8_t bpp) {
    gpu_copy_string(g_gpu_last_mode_name, sizeof(g_gpu_last_mode_name), name ? name : "unknown");
    g_gpu_last_mode_width = width;
    g_gpu_last_mode_height = height;
    g_gpu_last_mode_bpp = bpp;
}

void gpu_get_last_mode(char* out_name, size_t name_len, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    if (out_name && name_len) {
        gpu_copy_string(out_name, name_len, g_gpu_last_mode_name);
    }
    if (width) *width = g_gpu_last_mode_width;
    if (height) *height = g_gpu_last_mode_height;
    if (bpp) *bpp = g_gpu_last_mode_bpp;
}

void gpu_debug_log(const char* level, const char* msg) {
    if (!g_gpu_debug || !msg) return;
    console_write("[");
    if (level && *level) {
        console_write(level);
    } else {
        console_write("DBG");
    }
    console_write("] ");
    console_writeln(msg);
}

void gpu_debug_log_hex(const char* level, const char* label, uint32_t value) {
    if (!g_gpu_debug || !label) return;
    console_write("[");
    if (level && *level) {
        console_write(level);
    } else {
        console_write("DBG");
    }
    console_write("] ");
    console_write(label);
    console_write("=0x");
    console_write_hex32(value);
    console_writeln("");
}

void gpu_set_debug(int enabled) {
    g_gpu_debug = enabled ? 1 : 0;
    et4000_set_debug_trace(g_gpu_debug);
    console_write("gpu-debug: ");
    console_writeln(g_gpu_debug ? "enabled" : "disabled");
}

int gpu_get_debug(void) {
    return g_gpu_debug;
}

void gpu_print_status(void) {
    console_writeln("gpu-status:");
    console_write("  debug: ");
    console_writeln(g_gpu_debug ? "on" : "off");
    console_write("  last-mode: ");
    if (g_gpu_last_mode_width && g_gpu_last_mode_height) {
        console_write(g_gpu_last_mode_name);
        console_write(" ");
        console_write_dec(g_gpu_last_mode_width);
        console_write("x");
        console_write_dec(g_gpu_last_mode_height);
        console_write("x");
        console_write_dec(g_gpu_last_mode_bpp);
    } else {
        console_write("none");
    }
    console_writeln("");
    console_write("  last-error: ");
    console_writeln(g_gpu_last_error);
}

static void log_size(uint32_t bytes) {
    if (bytes == 0) {
        console_write("unknown");
        return;
    }
    const uint32_t one_kib = 1024u;
    const uint32_t one_mib = 1024u * 1024u;
    if (bytes % one_mib == 0) {
        console_write_dec(bytes / one_mib);
        console_write(" MiB");
    } else if (bytes % one_kib == 0) {
        console_write_dec(bytes / one_kib);
        console_write(" KiB");
    } else {
        console_write_dec(bytes);
        console_write(" bytes");
    }
}

static void log_caps(uint32_t caps) {
    int printed = 0;
    if (caps & GPU_CAP_LINEAR_FB) {
        console_write(" linear-fb");
        printed = 1;
    }
    if (caps & GPU_CAP_ACCEL_2D) {
        console_write(" 2d-accel");
        printed = 1;
    }
    if (caps & GPU_CAP_HW_CURSOR) {
        console_write(" hw-cursor");
        printed = 1;
    }
    if (caps & GPU_CAP_VBE_BIOS) {
        console_write(" vbe");
        printed = 1;
    }
    if (!printed) {
        console_write(" none");
    }
}

static void log_device(const gpu_info_t* gpu) {
    const char* name = (gpu->name[0] != '\0') ? gpu->name : "Unknown";
    console_write("GPU: ");
    console_write(name);
    if (gpu->pci.vendor_id || gpu->pci.device_id) {
        console_write(" (vendor=");
        console_write_hex16(gpu->pci.vendor_id);
        console_write(", device=");
        console_write_hex16(gpu->pci.device_id);
        console_write(") bus ");
        console_write_dec(gpu->pci.bus);
        console_write(", dev ");
        console_write_dec(gpu->pci.device);
        console_write(", fn ");
        console_write_dec(gpu->pci.function);
    } else {
        console_write(" (legacy/ISA)");
    }
    console_writeln("");

    console_write("      caps:");
    log_caps(gpu->capabilities);
    console_writeln("");

    if (gpu->framebuffer_bar != 0xFF) {
        console_write("      LFB BAR");
        console_write_dec((uint32_t)gpu->framebuffer_bar);
        console_write(" base=");
        console_write_hex32(gpu->framebuffer_base);
        console_write(", size=");
        log_size(gpu->framebuffer_size);
        console_writeln("");
    } else if (gpu->framebuffer_size) {
        console_write("      VRAM size=");
        log_size(gpu->framebuffer_size);
        console_writeln("");
    }

    if (gpu->type == GPU_TYPE_CIRRUS) {
        size_t mode_count = 0;
        const cirrus_mode_desc_t* modes = cirrus_get_modes(&mode_count);
        uint32_t vram = gpu->framebuffer_size;
        console_write("      modes:");
        int printed = 0;
        for (size_t i = 0; i < mode_count; i++) {
            const cirrus_mode_desc_t* mode = &modes[i];
            uint32_t required = cirrus_mode_vram_required(mode);
            if (required > 0 && required <= vram) {
                if (!printed) console_write(" ");
                else console_write(", ");
                console_write_dec((uint32_t)mode->width);
                console_write("x");
                console_write_dec((uint32_t)mode->height);
                console_write("x");
                console_write_dec((uint32_t)mode->bpp);
                printed = 1;
            }
        }
        if (!printed) {
            console_write(" none");
        }
        console_writeln("");
    } else if (gpu->type == GPU_TYPE_ET4000 || gpu->type == GPU_TYPE_ET4000AX) {
        console_write("      et4k-tests: toggle=");
        console_write(et4k_detection_toggle_ok() ? "OK" : "FAIL");
        console_write(", latch=");
        console_write(et4k_detection_latch_ok() ? "OK" : "FAIL");
        console_write(", signature=");
        console_write(et4k_detection_signature_ok() ? "OK" : "FAIL");
        console_writeln("");
        if (et4k_detection_alias_limited()) {
            uint32_t alias_bytes = et4k_detection_alias_limit_bytes();
            console_write("      bank-alias limit=");
            console_write_dec(alias_bytes ? alias_bytes / 1024u : 0u);
            console_writeln(" KB (bank 4 mirrors bank 0)");
        }
    }
}

void gpu_dump_registers(const gpu_info_t* gpu) {
    if (!gpu) {
        console_writeln("      (no adapter information available)");
        return;
    }

    switch (gpu->type) {
        case GPU_TYPE_CIRRUS:
            cirrus_dump_state(&gpu->pci);
            break;
        case GPU_TYPE_AVGA2:
            avga2_dump_state();
            et4000_debug_dump();
            break;
        case GPU_TYPE_ET4000:
        case GPU_TYPE_ET4000AX:
            et4000_debug_dump();
            break;
        default:
            console_writeln("      (no detailed dump available for this adapter)");
            break;
    }
}

static int tseng_mode_to_enum(uint16_t width, uint16_t height, uint8_t bpp, et4000_mode_t* out_mode);
static void tseng_enum_to_dimensions(et4000_mode_t mode, uint16_t* width, uint16_t* height, uint8_t* bpp);
static int tseng_candidate_contains(const et4000_mode_t* modes, size_t count, et4000_mode_t value);
static void tseng_candidate_append(et4000_mode_t* modes, size_t* count, size_t capacity, et4000_mode_t value);
static int activate_cirrus(uint16_t width, uint16_t height, uint8_t bpp);

size_t gpu_get_mode_catalog(const gpu_info_t* gpu,
                            gpu_mode_option_t* out_modes,
                            size_t capacity) {
    size_t count = 0;
    if (!gpu) {
        return 0;
    }

    switch (gpu->type) {
        case GPU_TYPE_CIRRUS: {
            size_t mode_count = 0;
            const cirrus_mode_desc_t* modes = cirrus_get_modes(&mode_count);
            uint32_t vram = gpu->framebuffer_size;
            for (size_t i = 0; i < mode_count; ++i) {
                const cirrus_mode_desc_t* mode = &modes[i];
                uint32_t required = cirrus_mode_vram_required(mode);
                if (required == 0) {
                    continue;
                }
                if (vram != 0 && required > vram) {
                    continue;
                }
                if (out_modes && count < capacity) {
                    out_modes[count].width = mode->width;
                    out_modes[count].height = mode->height;
                    out_modes[count].bpp = (uint8_t)mode->bpp;
                }
                ++count;
            }
            break;
        }
        case GPU_TYPE_ET4000:
        case GPU_TYPE_ET4000AX:
        case GPU_TYPE_AVGA2: {
            static const et4000_mode_t k_tseng_modes[] = {
                ET4000_MODE_640x480x8,
                ET4000_MODE_640x400x8,
                ET4000_MODE_640x480x4,
            };
            for (size_t i = 0; i < sizeof(k_tseng_modes) / sizeof(k_tseng_modes[0]); ++i) {
                et4000_mode_t mode = k_tseng_modes[i];
                if (gpu->type == GPU_TYPE_ET4000 && mode != ET4000_MODE_640x480x4) {
                    continue;
                }
                uint16_t w = 0;
                uint16_t h = 0;
                uint8_t bpp = 0;
                tseng_enum_to_dimensions(mode, &w, &h, &bpp);
                if (out_modes && count < capacity) {
                    out_modes[count].width = w;
                    out_modes[count].height = h;
                    out_modes[count].bpp = bpp;
                }
                ++count;
            }
            break;
        }
        default:
            break;
    }

    return count;
}

static gpu_info_t* gpu_acquire_slot_for_type(gpu_type_t type, const gpu_info_t* source);

static gpu_info_t g_gpu_infos[GPU_MAX_DEVICES];
static size_t g_gpu_count = 0;
static gpu_info_t* g_active_fb_gpu = NULL;
static int g_framebuffer_active = 0;
static int g_tseng_auto_enabled = 0;

void gpu_init(void) {
    et4000_set_debug_trace(g_gpu_debug);
    gpu_debug_log("INFO", "initialising GPU subsystem");
    g_gpu_count = 0;
    size_t pci_count = 0;
    int has_pci_gpu = 0;
    const pci_device_t* pci_devices = pci_get_devices(&pci_count);
    for (size_t i = 0; i < pci_count && g_gpu_count < GPU_MAX_DEVICES; i++) {
        const pci_device_t* dev = &pci_devices[i];
        gpu_info_t info = {0};
        info.type = GPU_TYPE_UNKNOWN;
        info.name[0] = '\0';
        info.framebuffer_bar = 0xFF;
        info.framebuffer_base = 0;
        info.framebuffer_size = 0;
        info.mmio_bar = 0;
        info.capabilities = 0;
        info.framebuffer_width = 0;
        info.framebuffer_height = 0;
        info.framebuffer_pitch = 0;
        info.framebuffer_bpp = 0;
        info.framebuffer_ptr = NULL;

        if (cirrus_gpu_detect(dev, &info)) {
            g_gpu_infos[g_gpu_count++] = info;
            if (info.pci.vendor_id || info.pci.device_id) {
                has_pci_gpu = 1;
            }
            continue;
        }
    }

#if CONFIG_VIDEO_ENABLE_ET4000
    if (!has_pci_gpu && g_gpu_count < GPU_MAX_DEVICES) {
        gpu_info_t info = {0};
        if (et4000_detect(&info)) {
            avga2_classify_info(&info);
            g_gpu_infos[g_gpu_count++] = info;
            if (!g_tseng_auto_enabled) {
                console_writeln("gpu: Tseng auto activation disabled (use 'gpuprobe auto' to re-enable)");
            }
        }
    }
#endif
}

const gpu_info_t* gpu_get_devices(size_t* count) {
    if (count) {
        *count = g_gpu_count;
    }
    return g_gpu_infos;
}

void gpu_log_summary(void) {
    if (g_gpu_count == 0) {
        console_writeln("GPU: no supported adapters detected.");
        return;
    }
    for (size_t i = 0; i < g_gpu_count; i++) {
        log_device(&g_gpu_infos[i]);
    }
}

void gpu_dump_details(void) {
    if (g_gpu_count == 0) {
        console_writeln("GPU: no supported adapters detected.");
        return;
    }
    for (size_t i = 0; i < g_gpu_count; i++) {
        const gpu_info_t* gpu = &g_gpu_infos[i];
        log_device(gpu);
        gpu_dump_registers(gpu);
    }
}

int gpu_request_framebuffer_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    if (g_framebuffer_active && g_active_fb_gpu) {
        if (g_active_fb_gpu->framebuffer_width == width &&
            g_active_fb_gpu->framebuffer_height == height &&
            g_active_fb_gpu->framebuffer_bpp == bpp) {
            return 1;
        }
    }

    for (size_t i = 0; i < g_gpu_count; i++) {
        gpu_info_t* gpu = &g_gpu_infos[i];
        if (gpu->type == GPU_TYPE_CIRRUS) {
            const cirrus_mode_desc_t* requested_mode = NULL;
            uint32_t vram = gpu->framebuffer_size;
            int explicit_request = (width != 0 || height != 0 || bpp != 0);
            if (explicit_request) {
                requested_mode = cirrus_find_mode(width, height, bpp, vram);
                if (!requested_mode) {
                    gpu_set_last_error("ERROR: unsupported Cirrus mode");
                    gpu_debug_log("ERROR", "Cirrus framebuffer request outside supported set");
                    continue;
                }
            } else {
                requested_mode = cirrus_default_mode(vram);
                if (!requested_mode) {
                    gpu_set_last_error("ERROR: no Cirrus modes fit VRAM");
                    gpu_debug_log("ERROR", "Cirrus default mode selection failed");
                    continue;
                }
            }

            display_mode_info_t mode;
            if (cirrus_set_mode_desc(&gpu->pci, requested_mode, &mode, gpu)) {
                display_manager_set_framebuffer_candidate("cirrus-lfb", &mode);
                display_manager_activate_framebuffer();
                cirrus_accel_enable(&mode);
                video_switch_to_framebuffer(&mode);
                gpu_set_last_mode(gpu->name[0] ? gpu->name : "Cirrus GD5446", mode.width, mode.height, mode.bpp);
                gpu_set_last_error("OK: Cirrus framebuffer active");
                g_active_fb_gpu = gpu;
                g_framebuffer_active = 1;
                return 1;
            }

            gpu_set_last_error("ERROR: Cirrus framebuffer activation failed");
            continue;
        }
#if CONFIG_VIDEO_ENABLE_ET4000
        if (gpu->type == GPU_TYPE_ET4000 ||
            gpu->type == GPU_TYPE_ET4000AX ||
            gpu->type == GPU_TYPE_AVGA2) {
            if (!g_tseng_auto_enabled) {
                console_writeln("gpu: Tseng auto activation suppressed (debug mode)");
                continue;
            }
            if (bpp == 8 || bpp == 4) {
                int explicit_request = (width != 0 || height != 0 || bpp != 0);
                et4000_mode_t candidates[4];
                size_t candidate_count = 0;
                et4000_mode_t requested_mode;
                if (tseng_mode_to_enum(width, height, bpp, &requested_mode)) {
                    tseng_candidate_append(candidates, &candidate_count, 4, requested_mode);
                } else if (explicit_request) {
                    gpu_set_last_error("ERROR: unsupported Tseng mode");
                    gpu_debug_log("ERROR", "Tseng framebuffer request outside supported set");
                    continue;
                }

                et4000_mode_t config_mode;
                switch (CONFIG_VIDEO_ET4000_MODE) {
                    case CONFIG_VIDEO_ET4000_MODE_640x400x8:
                        config_mode = ET4000_MODE_640x400x8;
                        break;
                    case CONFIG_VIDEO_ET4000_MODE_640x480x4:
                        config_mode = ET4000_MODE_640x480x4;
                        break;
                    default:
                        config_mode = ET4000_MODE_640x480x8;
                        break;
                }
                et4000_mode_t default_mode = et4k_choose_default_mode(gpu->type == GPU_TYPE_ET4000AX, gpu->framebuffer_size);

                tseng_candidate_append(candidates, &candidate_count, 4, config_mode);
                tseng_candidate_append(candidates, &candidate_count, 4, default_mode);
                tseng_candidate_append(candidates, &candidate_count, 4, ET4000_MODE_640x400x8);
                tseng_candidate_append(candidates, &candidate_count, 4, ET4000_MODE_640x480x4);

                display_mode_info_t mode;
                int activated = 0;
                uint16_t final_width = width;
                uint16_t final_height = height;
                uint8_t final_bpp = bpp;

                for (size_t ci = 0; ci < candidate_count && !activated; ++ci) {
                    uint16_t cw = 0;
                    uint16_t ch = 0;
                    uint8_t cb = 0;
                    tseng_enum_to_dimensions(candidates[ci], &cw, &ch, &cb);
                    if (et4k_set_mode(gpu, &mode, cw, ch, cb)) {
                        final_width = mode.width;
                        final_height = mode.height;
                        final_bpp = mode.bpp;
                        activated = 1;
                        break;
                    } else if (gpu_get_debug()) {
                        console_write("[et4k] candidate ");
                        console_write_dec(cw);
                        console_write("x");
                        console_write_dec(ch);
                        console_write("x");
                        console_write_dec(cb);
                        console_writeln(" failed");
                    }
                }

                if (activated) {
                    const char* driver_name = "et4000";
                    if (gpu->type == GPU_TYPE_ET4000AX) {
                        driver_name = "et4000ax";
                    } else if (gpu->type == GPU_TYPE_AVGA2) {
                        driver_name = "avga2";
                    }
                    display_manager_set_framebuffer_candidate(driver_name, &mode);
                    display_manager_activate_framebuffer();
                    video_switch_to_framebuffer(&mode);
                    gpu_set_last_mode(gpu->name[0] ? gpu->name : "et4000", final_width, final_height, final_bpp);
                    gpu_set_last_error("OK: Tseng framebuffer active");
                    g_active_fb_gpu = gpu;
                    g_framebuffer_active = 1;
                    return 1;
                }

                gpu_set_last_error("ERROR: Tseng framebuffer activation failed");
            }
            continue;
        }
#endif
    }
    return 0;
}

void gpu_restore_text_mode(void) {
    if (!g_framebuffer_active) return;

    if (g_active_fb_gpu && g_active_fb_gpu->type == GPU_TYPE_CIRRUS) {
        cirrus_restore_text_mode(&g_active_fb_gpu->pci);
        g_active_fb_gpu->framebuffer_ptr = NULL;
        g_active_fb_gpu->framebuffer_width = 0;
        g_active_fb_gpu->framebuffer_height = 0;
        g_active_fb_gpu->framebuffer_pitch = 0;
        g_active_fb_gpu->framebuffer_bpp = 0;
        cirrus_accel_disable();
    }
#if CONFIG_VIDEO_ENABLE_ET4000
    if (g_active_fb_gpu && (g_active_fb_gpu->type == GPU_TYPE_ET4000 ||
                            g_active_fb_gpu->type == GPU_TYPE_ET4000AX ||
                            g_active_fb_gpu->type == GPU_TYPE_AVGA2)) {
        et4000_restore_text_mode();
        g_active_fb_gpu->framebuffer_ptr = NULL;
        g_active_fb_gpu->framebuffer_width = 0;
        g_active_fb_gpu->framebuffer_height = 0;
        g_active_fb_gpu->framebuffer_pitch = 0;
        g_active_fb_gpu->framebuffer_bpp = 0;
    }
#endif

    g_active_fb_gpu = NULL;
    g_framebuffer_active = 0;
    display_manager_activate_text();
    video_switch_to_text();
    gpu_set_last_mode("text-mode", 0, 0, 0);
    gpu_set_last_error("OK: text mode restored");
}

void gpu_debug_probe(int scan_legacy) {
    console_writeln("gpu-probe: stored device table");
    size_t count = 0;
    const gpu_info_t* gpus = gpu_get_devices(&count);
    if (count == 0 || !gpus) {
        console_writeln("  (no GPU entries recorded)");
    } else {
        for (size_t i = 0; i < count; i++) {
            const gpu_info_t* gpu = &gpus[i];
            console_write("  #");
            console_write_dec((uint32_t)i);
            console_write(": type=");
            switch (gpu->type) {
                case GPU_TYPE_CIRRUS: console_write("cirrus gd5446"); break;
                case GPU_TYPE_ET4000: console_write("et4000"); break;
                case GPU_TYPE_ET4000AX: console_write("et4000ax"); break;
                case GPU_TYPE_AVGA2: console_write("avga2"); break;
                default: console_write("unknown"); break;
            }
            console_write(" name=\"");
            console_write(gpu->name);
            console_write("\" phys=0x");
            console_write_hex32(gpu->framebuffer_base);
            console_write(" size=0x");
            console_write_hex32(gpu->framebuffer_size);
            console_write("\n");
        }
    }
    if (scan_legacy) {
#if CONFIG_VIDEO_ENABLE_ET4000
        console_writeln("gpu-probe: Tseng driver enabled (CONFIG_VIDEO_ENABLE_ET4000=1)");
#else
        console_writeln("gpu-probe: Tseng driver disabled (CONFIG_VIDEO_ENABLE_ET4000=0)");
#endif
        if (avga2_signature_present()) {
            console_writeln("gpu-probe: Acumos AVGA2 BIOS signature located");
        } else {
            console_writeln("gpu-probe: no Acumos AVGA2 BIOS signature detected");
        }
        console_writeln("gpu-probe: running Tseng ET4000 manual scan");
        gpu_info_t info = {0};
        et4000_set_debug_trace(1);
        int tseng_found = et4000_detect(&info);
        et4000_set_debug_trace(0);
        if (tseng_found) {
            avga2_classify_info(&info);
            gpu_info_t* slot = gpu_acquire_slot_for_type(info.type, &info);
            if (!slot) {
                console_writeln("  scan result: gpu table full, unable to store Tseng adapter");
            }
            console_write("  scan result: detected ");
            console_write(info.name[0] ? info.name : "Tseng ET4000");
            console_write(" (type=");
            const char* type_name = "et4000";
            if (info.type == GPU_TYPE_ET4000AX) {
                type_name = "et4000ax";
            } else if (info.type == GPU_TYPE_AVGA2) {
                type_name = "avga2";
            }
            console_write(type_name);
            console_write(")\n");
        } else {
            console_writeln("  scan result: no Tseng ET4000 signature");
        }
        et4000_set_debug_trace(1);
        et4000_debug_dump();
        et4000_set_debug_trace(0);
    }
    console_writeln("gpu-probe: done");
}

static gpu_info_t* gpu_acquire_slot_for_type(gpu_type_t type, const gpu_info_t* source) {
    gpu_info_t* slot = NULL;
    for (size_t i = 0; i < g_gpu_count; i++) {
        if (g_gpu_infos[i].type == type) {
            slot = &g_gpu_infos[i];
            break;
        }
    }
    if (!slot) {
        if (g_gpu_count >= GPU_MAX_DEVICES) {
            return NULL;
        }
        slot = &g_gpu_infos[g_gpu_count++];
    }
    if (source) {
        *slot = *source;
        slot->type = type;
    } else {
        gpu_memzero(slot, sizeof(*slot));
        slot->type = type;
    }
    return slot;
}

static int tseng_mode_to_enum(uint16_t width, uint16_t height, uint8_t bpp, et4000_mode_t* out_mode) {
    if (width == 640 && height == 480 && bpp == 8) {
        if (out_mode) *out_mode = ET4000_MODE_640x480x8;
        return 1;
    }
    if (width == 640 && height == 400 && bpp == 8) {
        if (out_mode) *out_mode = ET4000_MODE_640x400x8;
        return 1;
    }
    if (width == 640 && height == 480 && bpp == 4) {
        if (out_mode) *out_mode = ET4000_MODE_640x480x4;
        return 1;
    }
    return 0;
}

static void tseng_enum_to_dimensions(et4000_mode_t mode, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    switch (mode) {
        case ET4000_MODE_640x480x8:
            if (width) *width = 640;
            if (height) *height = 480;
            if (bpp) *bpp = 8;
            break;
        case ET4000_MODE_640x400x8:
            if (width) *width = 640;
            if (height) *height = 400;
            if (bpp) *bpp = 8;
            break;
        case ET4000_MODE_640x480x4:
        default:
            if (width) *width = 640;
            if (height) *height = 480;
            if (bpp) *bpp = 4;
            break;
    }
}

static int tseng_candidate_contains(const et4000_mode_t* modes, size_t count, et4000_mode_t value) {
    for (size_t i = 0; i < count; ++i) {
        if (modes[i] == value) {
            return 1;
        }
    }
    return 0;
}

static void tseng_candidate_append(et4000_mode_t* modes, size_t* count, size_t capacity, et4000_mode_t value) {
    if (!modes || !count) return;
    if (tseng_candidate_contains(modes, *count, value)) return;
    if (*count >= capacity) return;
    modes[*count] = value;
    (*count)++;
}

static int activate_cirrus(uint16_t width, uint16_t height, uint8_t bpp) {
    gpu_info_t* gpu = NULL;
    for (size_t i = 0; i < g_gpu_count; ++i) {
        if (g_gpu_infos[i].type == GPU_TYPE_CIRRUS) {
            gpu = &g_gpu_infos[i];
            break;
        }
    }
    if (!gpu) {
        gpu_set_last_error("ERROR: no Cirrus adapter detected");
        gpu_debug_log("ERROR", "Cirrus manual activation requested but no adapter present");
        return 0;
    }

    const cirrus_mode_desc_t* mode = NULL;
    uint32_t vram = gpu->framebuffer_size;
    int explicit_mode = (width != 0 && height != 0 && bpp != 0);
    if (explicit_mode) {
        mode = cirrus_find_mode(width, height, bpp, vram);
        if (!mode) {
            gpu_set_last_error("ERROR: unsupported Cirrus mode");
            gpu_debug_log("ERROR", "Cirrus manual activation requested unsupported mode");
            return 0;
        }
    } else {
        mode = cirrus_default_mode(vram);
        if (!mode) {
            gpu_set_last_error("ERROR: no Cirrus framebuffer modes fit VRAM");
            gpu_debug_log("ERROR", "Cirrus default mode selection failed");
            return 0;
        }
    }

    display_mode_info_t fb_mode;
    if (!cirrus_set_mode_desc(&gpu->pci, mode, &fb_mode, gpu)) {
        gpu_set_last_error("ERROR: Cirrus set_mode failed");
        gpu_debug_log("ERROR", "cirrus_set_mode_desc() failed");
        return 0;
    }

    display_manager_set_framebuffer_candidate("cirrus-lfb", &fb_mode);
    display_manager_activate_framebuffer();
    cirrus_accel_enable(&fb_mode);
    video_switch_to_framebuffer(&fb_mode);
    g_active_fb_gpu = gpu;
    g_framebuffer_active = 1;
    gpu_set_last_mode(gpu->name[0] ? gpu->name : "Cirrus GD5446", fb_mode.width, fb_mode.height, fb_mode.bpp);
    gpu_set_last_error("OK: Cirrus framebuffer active");
    gpu_debug_log("OK", "Cirrus framebuffer active");
    return 1;
}

static int activate_tseng(uint16_t width, uint16_t height, uint8_t bpp, int force_ax_variant) {
#if !CONFIG_VIDEO_ENABLE_ET4000
    (void)width; (void)height; (void)bpp; (void)force_ax_variant;
    gpu_set_last_error("ERROR: Tseng driver disabled at build time");
    return 0;
#else
    int explicit_mode = (width != 0 && height != 0 && bpp != 0);
    et4000_mode_t desired_mode = ET4000_MODE_640x480x8;
    if (explicit_mode) {
        if (!tseng_mode_to_enum(width, height, bpp, &desired_mode)) {
            gpu_set_last_error("ERROR: unsupported Tseng mode");
            gpu_debug_log("ERROR", "unsupported Tseng mode requested");
            return 0;
        }
    }

    if (force_ax_variant > 0) {
        gpu_debug_log("INFO", "forcing ET4000AX variant");
    } else if (force_ax_variant == 0) {
        gpu_debug_log("INFO", "forcing ET4000 (non-AX) variant");
    } else {
        gpu_debug_log("INFO", "auto-selecting Tseng variant");
    }

    et4000_set_debug_trace(g_gpu_debug);

    gpu_info_t detected = {0};
    if (!et4000_detect(&detected)) {
        gpu_set_last_error("ERROR: Tseng adapter not detected");
        gpu_debug_log("ERROR", "Tseng signature not found");
        return 0;
    }
    avga2_classify_info(&detected);
    int detected_is_ax = (detected.type == GPU_TYPE_ET4000AX);
    int target_is_ax = detected_is_ax;

    if (force_ax_variant == 1 && detected.type != GPU_TYPE_ET4000AX) {
        target_is_ax = 1;
        g_is_ax_variant = 1;
        detected.type = GPU_TYPE_ET4000AX;
        gpu_copy_string(detected.name, sizeof(detected.name), "Tseng ET4000AX (forced)");
        gpu_debug_log("WARN", "forcing AX variant despite detection");
    } else if (force_ax_variant == 0 && detected.type == GPU_TYPE_ET4000AX) {
        target_is_ax = 0;
        g_is_ax_variant = 0;
        detected.type = GPU_TYPE_ET4000;
        gpu_copy_string(detected.name, sizeof(detected.name), "Tseng ET4000 (forced)");
        gpu_debug_log("WARN", "forcing non-AX variant");
    } else {
        g_is_ax_variant = target_is_ax;
    }

    if (!explicit_mode) {
        et4000_mode_t auto_mode = et4k_choose_default_mode(target_is_ax, detected.framebuffer_size);
        desired_mode = auto_mode;
        tseng_enum_to_dimensions(auto_mode, &width, &height, &bpp);
        switch (auto_mode) {
            case ET4000_MODE_640x480x8:
                gpu_debug_log("INFO", "Tseng auto-selecting 640x480x8");
                break;
            case ET4000_MODE_640x400x8:
                gpu_debug_log("INFO", "Tseng auto-selecting 640x400x8");
                break;
            default:
                gpu_debug_log("INFO", "Tseng auto-selecting 640x480x4");
                break;
        }
    }

    gpu_info_t* slot = gpu_acquire_slot_for_type(detected.type, &detected);
    if (!slot) {
        gpu_set_last_error("ERROR: GPU table full");
        gpu_debug_log("ERROR", "unable to reserve GPU slot");
        return 0;
    }

    display_mode_info_t mode;
    if (!et4k_set_mode(slot, &mode, width, height, bpp)) {
        gpu_debug_log("ERROR", "et4k_set_mode() failed");
        if (gpu_get_last_error()[0] == '\0') {
            gpu_set_last_error("ERROR: Tseng set_mode failed");
        }
        return 0;
    }

    const char* driver_name = target_is_ax ? "et4000ax" : "et4000";
    if (!target_is_ax && detected.type == GPU_TYPE_AVGA2) {
        driver_name = "avga2";
    }
    display_manager_set_framebuffer_candidate(driver_name, &mode);
    display_manager_activate_framebuffer();
    video_switch_to_framebuffer(&mode);
    g_active_fb_gpu = slot;
    g_framebuffer_active = 1;

    gpu_set_last_mode(slot->name[0] ? slot->name : "Tseng", mode.width, mode.height, mode.bpp);
    gpu_set_last_error("OK: Tseng framebuffer active");
    gpu_debug_log("OK", "Tseng framebuffer active");
    return 1;
#endif
}

int gpu_manual_activate_et4000(uint16_t width, uint16_t height, uint8_t bpp) {
    return activate_tseng(width, height, bpp, -1);
}

int gpu_force_activate(gpu_type_t type, uint16_t width, uint16_t height, uint8_t bpp) {
    switch (type) {
        case GPU_TYPE_ET4000:
            return activate_tseng(width, height, bpp, 0);
        case GPU_TYPE_ET4000AX:
            return activate_tseng(width, height, bpp, 1);
        case GPU_TYPE_AVGA2:
            return activate_tseng(width, height, bpp, -1);
        case GPU_TYPE_VGA:
            gpu_restore_text_mode();
            gpu_debug_log("INFO", "switched to VGA text mode");
            return 1;
        case GPU_TYPE_CIRRUS:
            return activate_cirrus(width, height, bpp);
        default:
            gpu_set_last_error("ERROR: unsupported GPU type");
            return 0;
    }
}

void gpu_tseng_set_auto_enabled(int enabled) {
#if CONFIG_VIDEO_ENABLE_ET4000
    g_tseng_auto_enabled = enabled ? 1 : 0;
    console_write("gpu: Tseng auto activation ");
    console_writeln(g_tseng_auto_enabled ? "enabled" : "disabled");
    if (!g_tseng_auto_enabled && g_active_fb_gpu &&
        (g_active_fb_gpu->type == GPU_TYPE_ET4000 ||
         g_active_fb_gpu->type == GPU_TYPE_ET4000AX ||
         g_active_fb_gpu->type == GPU_TYPE_AVGA2)) {
        console_writeln("gpu: auto disabled while framebuffer active – consider gpuinfo or gpuprobe activate to re-enable manually");
    }
#else
    (void)enabled;
#endif
}

int gpu_tseng_get_auto_enabled(void) {
#if CONFIG_VIDEO_ENABLE_ET4000
    return g_tseng_auto_enabled;
#else
    return 0;
#endif
}
