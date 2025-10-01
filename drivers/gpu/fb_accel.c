#include "fb_accel.h"
#include <stddef.h>

typedef struct {
    const fb_accel_ops_t* ops;
    void* ctx;
} fb_accel_state_t;

static fb_accel_state_t g_fb_accel = { NULL, NULL };

void fb_accel_register(const fb_accel_ops_t* ops, void* ctx) {
    g_fb_accel.ops = ops;
    g_fb_accel.ctx = ctx;
}

void fb_accel_reset(void) {
    g_fb_accel.ops = NULL;
    g_fb_accel.ctx = NULL;
}

int fb_accel_available(void) {
    return (g_fb_accel.ops && g_fb_accel.ops->fill_rect);
}

int fb_accel_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    if (!g_fb_accel.ops || !g_fb_accel.ops->fill_rect) {
        return 0;
    }
    return g_fb_accel.ops->fill_rect(g_fb_accel.ctx, x, y, width, height, color);
}

void fb_accel_sync(void) {
    if (g_fb_accel.ops && g_fb_accel.ops->sync) {
        g_fb_accel.ops->sync(g_fb_accel.ctx);
    }
}

void fb_accel_mark_dirty(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    if (g_fb_accel.ops && g_fb_accel.ops->mark_dirty) {
        g_fb_accel.ops->mark_dirty(g_fb_accel.ctx, x, y, width, height);
    }
}
