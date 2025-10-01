#ifndef DRIVERS_GPU_FB_ACCEL_H
#define DRIVERS_GPU_FB_ACCEL_H

#include <stdint.h>

typedef struct fb_accel_ops {
    int  (*fill_rect)(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);
    void (*sync)(void* ctx);
    void (*mark_dirty)(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
} fb_accel_ops_t;

void fb_accel_register(const fb_accel_ops_t* ops, void* ctx);
void fb_accel_reset(void);
int  fb_accel_available(void);
int  fb_accel_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);
void fb_accel_sync(void);
void fb_accel_mark_dirty(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

#endif // DRIVERS_GPU_FB_ACCEL_H
