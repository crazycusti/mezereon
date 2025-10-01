#include "cirrus_accel.h"
#include "fb_accel.h"
#include "vga_hw.h"
#include "../../config.h"

#if CONFIG_ARCH_X86
#include "../../arch/x86/io.h"
#endif

#include <stddef.h>

// Cirrus BitBLT register indices (graphics controller space)
#define CIRRUS_GR_BLT_WIDTH_LOW      0x20
#define CIRRUS_GR_BLT_WIDTH_HIGH     0x21
#define CIRRUS_GR_BLT_HEIGHT_LOW     0x22
#define CIRRUS_GR_BLT_HEIGHT_HIGH    0x23
#define CIRRUS_GR_BLT_DST_PITCH_LOW  0x24
#define CIRRUS_GR_BLT_DST_PITCH_HIGH 0x25
#define CIRRUS_GR_BLT_SRC_PITCH_LOW  0x26
#define CIRRUS_GR_BLT_SRC_PITCH_HIGH 0x27
#define CIRRUS_GR_BLT_DST_START_LOW  0x28
#define CIRRUS_GR_BLT_DST_START_MID  0x29
#define CIRRUS_GR_BLT_DST_START_HIGH 0x2A
#define CIRRUS_GR_BLT_SRC_START_LOW  0x2C
#define CIRRUS_GR_BLT_SRC_START_MID  0x2D
#define CIRRUS_GR_BLT_SRC_START_HIGH 0x2E
#define CIRRUS_GR_BLT_MODE           0x30
#define CIRRUS_GR_BLT_STATUS         0x31
#define CIRRUS_GR_BLT_ROP            0x32

#define CIRRUS_BLT_STATUS_BUSY       0x08
#define CIRRUS_BLT_MODE_PATTERNCOPY  0x40
#define CIRRUS_BLT_MODE_COLOREXPAND  0x80
#define CIRRUS_BLT_ROP_SRCCOPY       0x0D

#define VGA_GFX_SR_VALUE   0x00
#define VGA_GFX_SR_ENABLE  0x01

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
    int      enabled;
} cirrus_accel_ctx_t;

static cirrus_accel_ctx_t g_ctx = {0, 0, 0, 0, 0};

static void cirrus_wait_idle(void) {
#if CONFIG_ARCH_X86
    while (vga_gc_read(CIRRUS_GR_BLT_STATUS) & CIRRUS_BLT_STATUS_BUSY) {
        // busy-wait; engine is quick for tiny rectangles
    }
#endif
}

static int cirrus_fill_rect(void* ctx_ptr, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    (void)ctx_ptr;
#if !CONFIG_ARCH_X86
    (void)x; (void)y; (void)width; (void)height; (void)color;
    return 0;
#else
    if (!g_ctx.enabled) return 0;
    if (g_ctx.bpp != 8) return 0;
    if (!width || !height) return 1;
    if (x >= g_ctx.width || y >= g_ctx.height) return 0;
    if ((uint32_t)x + (uint32_t)width > g_ctx.width) return 0;
    if ((uint32_t)y + (uint32_t)height > g_ctx.height) return 0;

    uint32_t pitch = g_ctx.pitch;
    if (pitch == 0 || pitch > 0xFFFFu) return 0;

    uint32_t dest = (uint32_t)y * pitch + (uint32_t)x;
    uint16_t w_minus = (uint16_t)(width - 1u);
    uint16_t h_minus = (uint16_t)(height - 1u);

    cirrus_wait_idle();

    vga_gc_write(VGA_GFX_SR_VALUE, color);
    vga_gc_write(VGA_GFX_SR_ENABLE, color);

    vga_gc_write(CIRRUS_GR_BLT_DST_PITCH_LOW, (uint8_t)(pitch & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_DST_PITCH_HIGH, (uint8_t)((pitch >> 8) & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_SRC_PITCH_LOW, (uint8_t)(pitch & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_SRC_PITCH_HIGH, (uint8_t)((pitch >> 8) & 0xFFu));

    vga_gc_write(CIRRUS_GR_BLT_WIDTH_LOW, (uint8_t)(w_minus & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_WIDTH_HIGH, (uint8_t)((w_minus >> 8) & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_HEIGHT_LOW, (uint8_t)(h_minus & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_HEIGHT_HIGH, (uint8_t)((h_minus >> 8) & 0xFFu));

    vga_gc_write(CIRRUS_GR_BLT_DST_START_LOW, (uint8_t)(dest & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_DST_START_MID, (uint8_t)((dest >> 8) & 0xFFu));
    vga_gc_write(CIRRUS_GR_BLT_DST_START_HIGH, (uint8_t)((dest >> 16) & 0xFFu));

    vga_gc_write(CIRRUS_GR_BLT_SRC_START_LOW, 0);
    vga_gc_write(CIRRUS_GR_BLT_SRC_START_MID, 0);
    vga_gc_write(CIRRUS_GR_BLT_SRC_START_HIGH, 0);

    vga_gc_write(CIRRUS_GR_BLT_MODE, (uint8_t)(CIRRUS_BLT_MODE_COLOREXPAND | CIRRUS_BLT_MODE_PATTERNCOPY));
    vga_gc_write(CIRRUS_GR_BLT_ROP, CIRRUS_BLT_ROP_SRCCOPY);

    vga_gc_write(CIRRUS_GR_BLT_STATUS, 0x02u);

    cirrus_wait_idle();
    return 1;
#endif
}

static void cirrus_sync(void* ctx_ptr) {
    (void)ctx_ptr;
    cirrus_wait_idle();
}

static const fb_accel_ops_t g_ops = {
    .fill_rect = cirrus_fill_rect,
    .sync = cirrus_sync,
};

void cirrus_accel_enable(const display_mode_info_t* mode) {
    if (!mode) {
        g_ctx.enabled = 0;
        fb_accel_reset();
        return;
    }
    g_ctx.width = mode->width;
    g_ctx.height = mode->height;
    g_ctx.pitch = mode->pitch;
    g_ctx.bpp = mode->bpp;
    g_ctx.enabled = (mode->bpp == 8);
    if (g_ctx.enabled) {
        fb_accel_register(&g_ops, &g_ctx);
    } else {
        fb_accel_reset();
    }
}

void cirrus_accel_disable(void) {
    g_ctx.enabled = 0;
    fb_accel_reset();
}
