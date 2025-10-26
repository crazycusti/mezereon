#include "fb_patterns.h"
#include "../drivers/gpu/vga_hw.h"

static uint16_t min_u16(uint16_t a, uint16_t b) {
    return (a < b) ? a : b;
}

void fb_patterns_configure_palette(void) {
    vga_dac_load_default_palette();
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0x1F) << 11) |
                      ((uint16_t)(g & 0x3F) << 5) |
                      ((uint16_t)(b & 0x1F)));
}

static void draw_8bpp_demo(volatile uint8_t* fb,
                           uint16_t width,
                           uint16_t height,
                           uint32_t pitch) {
    if (!fb || width == 0 || height == 0 || pitch == 0) {
        return;
    }

    uint16_t section = height / 3;
    if (section == 0) {
        section = height;
    }

    for (uint16_t y = 0; y < section; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint16_t band_count = width / 16 ? width / 16 : 1;
            uint8_t color = (uint8_t)((x / band_count) & 0x0F);
            fb[y * pitch + x] = color;
        }
    }

    for (uint16_t y = section; y < min_u16((uint16_t)(section * 2), height); y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t value = (width > 1) ? (uint8_t)((x * 255u) / (width - 1)) : 0;
            fb[y * pitch + x] = value;
        }
    }

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

static void draw_16bpp_demo(volatile uint8_t* fb_bytes,
                            uint16_t width,
                            uint16_t height,
                            uint32_t pitch) {
    if (!fb_bytes || width == 0 || height == 0 || pitch < (uint32_t)(width * 2)) {
        return;
    }

    volatile uint16_t* fb = (volatile uint16_t*)fb_bytes;
    uint32_t stride = pitch / 2u;
    uint16_t section = height / 3;
    if (section == 0) {
        section = height;
    }

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
            uint8_t r = (uint8_t)((x * 31u) / (width > 1 ? (width - 1) : 1));
            uint8_t g = (uint8_t)(((height - 1 - y) * 63u) / (section ? section : 1));
            uint8_t b = (uint8_t)((y * 31u) / (height > 1 ? (height - 1) : 1));
            fb[y * stride + x] = rgb565(r, g, b);
        }
    }

    for (uint16_t y = (uint16_t)(section * 2); y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t checker = (uint8_t)(((x / 32u) ^ (y / 16u)) & 0x01u);
            fb[y * stride + x] = checker ? rgb565(0x1Fu, 0x3Fu, 0x00u) : rgb565(0x00u, 0x00u, 0x00u);
        }
    }
}

void fb_patterns_draw_demo(volatile uint8_t* fb,
                           uint16_t width,
                           uint16_t height,
                           uint32_t pitch,
                           uint8_t bpp) {
    if (!fb || width == 0 || height == 0 || pitch == 0) {
        return;
    }

    if (bpp == 8) {
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb[y * pitch + x] = 0;
            }
        }
        draw_8bpp_demo(fb, width, height, pitch);
    } else if (bpp == 16) {
        volatile uint16_t* fb16 = (volatile uint16_t*)fb;
        uint32_t stride = pitch / 2u;
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb16[y * stride + x] = 0;
            }
        }
        draw_16bpp_demo(fb, width, height, pitch);
    } else {
        for (uint16_t y = 0; y < height; y++) {
            for (uint16_t x = 0; x < width; x++) {
                fb[y * pitch + x] = (uint8_t)((x + y) & 0xFFu);
            }
        }
    }
}
