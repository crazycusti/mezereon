#include "gpu.h"
#include "cirrus.h"
#include "../../console.h"
#include "../../display.h"
#include "../../video_fb.h"
#include <stdint.h>
#include <stddef.h>

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
    }
}

static gpu_info_t g_gpu_infos[GPU_MAX_DEVICES];
static size_t g_gpu_count = 0;
static gpu_info_t* g_active_fb_gpu = NULL;
static int g_framebuffer_active = 0;

void gpu_init(void) {
    g_gpu_count = 0;
    size_t pci_count = 0;
    const pci_device_t* pci_devices = pci_get_devices(&pci_count);
    for (size_t i = 0; i < pci_count && g_gpu_count < GPU_MAX_DEVICES; i++) {
        const pci_device_t* dev = &pci_devices[i];
        gpu_info_t info;
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
            continue;
        }
    }
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
        switch (gpu->type) {
            case GPU_TYPE_CIRRUS:
                cirrus_dump_state(&gpu->pci);
                break;
            default:
                console_writeln("      (no detailed dump available for this adapter)");
                break;
        }
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
            if (width == 640 && height == 480 && bpp == 8) {
                display_mode_info_t mode;
                if (cirrus_set_mode_640x480x8(&gpu->pci, &mode, gpu)) {
                    display_manager_set_framebuffer_candidate("cirrus-lfb", &mode);
                    display_manager_activate_framebuffer();
                    video_switch_to_framebuffer(&mode);
                    g_active_fb_gpu = gpu;
                    g_framebuffer_active = 1;
                    return 1;
                }
            }
        }
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
    }

    g_active_fb_gpu = NULL;
    g_framebuffer_active = 0;
    display_manager_activate_text();
    video_switch_to_text();
}
