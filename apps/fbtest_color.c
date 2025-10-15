#include "fbtest_color.h"
#include "../display.h"
#include "../config.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include "../drivers/gpu/vga_hw.h"
#include "../keyboard.h"
#include "../cpuidle.h"
#include "../netface.h"
#include <stdint.h>

extern void video_init(void);

static uint16_t min_u16(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

static void fbtest_configure_palette(void) {
    vga_dac_load_default_palette();
}

static void draw_8bpp_demo(volatile uint8_t* fb, uint16_t width, uint16_t height, uint32_t pitch) {
    if (!fb || width == 0 || height == 0 || pitch == 0) return;
    uint16_t section = height / 3;
    if (section == 0) section = height;

    // Oberes Drittel: 16 Farbbalken (Basispalette)
    for (uint16_t y = 0; y < section; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint16_t band_count = width / 16 ? width / 16 : 1;
            uint8_t color = (uint8_t)((x / band_count) & 0x0F);
            fb[y * pitch + x] = color;
        }
    }

    // Mittleres Drittel: glatter Verlauf 0..255
    for (uint16_t y = section; y < min_u16((uint16_t)(section * 2), height); y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t value = (width > 1) ? (uint8_t)((x * 255u) / (width - 1)) : 0;
            fb[y * pitch + x] = value;
        }
    }

    // Unteres Drittel: 16x16 Palettenraster (alle 256 VGA-Farben)
    uint16_t grid_start = (uint16_t)(section * 2);
    uint16_t grid_height = height > grid_start ? (uint16_t)(height - grid_start) : 1;
    for (uint16_t y = grid_start; y < height; y++) {
        uint16_t rel_y = (uint16_t)(y - grid_start);
        uint16_t block_y = (uint16_t)(((uint32_t)rel_y * 16u) / grid_height);
        if (block_y > 15) block_y = 15;
        for (uint16_t x = 0; x < width; x++) {
            uint16_t block_x = (uint16_t)(((uint32_t)x * 16u) / (width ? width : 1u));
            if (block_x > 15) block_x = 15;
            uint8_t tile = (uint8_t)((block_y << 4) | block_x);
            fb[y * pitch + x] = tile;
        }
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}

static void draw_16bpp_demo(volatile uint8_t* fb_bytes, uint16_t width, uint16_t height, uint32_t pitch) {
    if (!fb_bytes || width == 0 || height == 0 || pitch < (uint32_t)(width * 2)) return;
    volatile uint16_t* fb = (volatile uint16_t*)fb_bytes;
    uint32_t stride = pitch / 2;
    uint16_t section = height / 3;
    if (section == 0) section = height;

    static const uint16_t palette16[16] = {
        0x0000, 0xF800, 0x07E0, 0x001F,
        0xFFE0, 0x07FF, 0xF81F, 0xFFFF,
        0x7BEF, 0xFAE0, 0x041F, 0x8210,
        0xFA10, 0x83F0, 0x821F, 0x781F
    };

    for (uint16_t y = 0; y < section; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint16_t band_count = width / 16 ? width / 16 : 1;
            uint8_t idx = (uint8_t)((x / band_count) & 0x0F);
            fb[y * stride + x] = palette16[idx];
        }
    }

    for (uint16_t y = section; y < min_u16((uint16_t)(section * 2), height); y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t r = (uint8_t)((x * 31u) / (width - 1 ? width - 1 : 1));
            uint8_t g = (uint8_t)(((height - 1 - y) * 63u) / (section ? section : 1));
            uint8_t b = (uint8_t)((y * 31u) / (height - 1 ? height - 1 : 1));
            fb[y * stride + x] = rgb565(r, g, b);
        }
    }

    for (uint16_t y = (uint16_t)(section * 2); y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t checker = (uint8_t)(((x / 32) ^ (y / 16)) & 0x01);
            fb[y * stride + x] = checker ? rgb565(0x1F, 0x3F, 0x00) : rgb565(0x00, 0x00, 0x00);
        }
    }
}

static void wait_for_keypress(void) {
    for (;;) {
        int c = keyboard_poll_char();
        if (c >= 0) {
            break;
        }
        netface_poll();
        cpuidle_idle();
    }
}

void fbtest_run(void) {
    console_writeln("fbtest: versuche Framebuffer 640x480/640x400 @ 8bpp. Taste drücken für Abbruch.");

    uint16_t preferred_height = 480;
#if CONFIG_VIDEO_ENABLE_ET4000
    if (CONFIG_VIDEO_ET4000_MODE == CONFIG_VIDEO_ET4000_MODE_640x400) {
        preferred_height = 400;
    }
#endif
    int fb_enabled = gpu_request_framebuffer_mode(640, preferred_height, 8);
    if (!fb_enabled && preferred_height != 480) {
        fb_enabled = gpu_request_framebuffer_mode(640, 480, 8);
    }
    if (!fb_enabled) {
        console_writeln("fbtest: kein unterstützter Framebuffer gefunden.");
        return;
    }

    const display_state_t* st = display_manager_state();
    if (!st || !(st->active_features & DISPLAY_FEATURE_FRAMEBUFFER) || !st->active_mode.framebuffer) {
        console_writeln("fbtest: Framebuffer-Zustand unerwartet -> breche ab.");
        gpu_restore_text_mode();
        video_init();
        return;
    }

    volatile uint8_t* fb = st->active_mode.framebuffer;
    uint16_t width = st->active_mode.width;
    uint16_t height = st->active_mode.height;
    uint32_t pitch = st->active_mode.pitch;
    uint8_t bpp = st->active_mode.bpp;

    fbtest_configure_palette();

    if (bpp == 8) {
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb[y * pitch + x] = 0;
            }
        }
        draw_8bpp_demo(fb, width, height, pitch);
    } else if (bpp == 16) {
        volatile uint16_t* fb16 = (volatile uint16_t*)fb;
        uint32_t stride = pitch / 2;
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb16[y * stride + x] = 0;
            }
        }
        draw_16bpp_demo(fb, width, height, pitch);
    } else {
        // Nicht unterstütztes Format -> Bildschirm schwärzen
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb[y * pitch + x] = 0;
            }
        }
    }

    wait_for_keypress();

    gpu_restore_text_mode();
    video_init();
    console_clear();
    console_writeln("fbtest: Textmodus wieder aktiv.");
    display_manager_log_state();
}
