#include "mezapi.h"
#include "console.h"
#include "keyboard.h"
#include "platform.h"
#include "drivers/pcspeaker.h"
#include "drivers/sb16.h"
#include "drivers/gpu/fb_accel.h"
#include "video_fb.h"
#include <stddef.h>
#include <stdint.h>

// Minimal backend wrappers (text-mode drawing via VGA text memory for now)

#define VGA_MEM ((volatile uint16_t*)0xB8000)
#ifndef CONFIG_VGA_WIDTH
#define CONFIG_VGA_WIDTH 80
#endif
#ifndef CONFIG_VGA_HEIGHT
#define CONFIG_VGA_HEIGHT 25
#endif

static void api_text_put(int x, int y, char ch, uint8_t attr){
    if (x<0||y<0||x>=CONFIG_VGA_WIDTH||y>=CONFIG_VGA_HEIGHT) return;
    VGA_MEM[y*CONFIG_VGA_WIDTH + x] = (uint16_t)(((uint16_t)attr<<8) | (uint8_t)ch);
}

static void api_text_fill_line(int y, char ch, uint8_t attr){
    if (y<0||y>=CONFIG_VGA_HEIGHT) return;
    for (int x=0;x<CONFIG_VGA_WIDTH;x++) api_text_put(x,y,ch,attr);
}

static void api_time_sleep_ms(uint32_t ms){
    uint32_t hz = platform_timer_get_hz(); if (!hz) hz=100;
    uint32_t start = platform_ticks_get();
    uint32_t ticks = (ms * hz + 999u) / 1000u;
    while ((platform_ticks_get() - start) < ticks) { /* spin */ }
}

static void api_sound_tone_on(uint32_t hz){ if (hz){ pcspeaker_set_freq(hz); pcspeaker_on(); } }
static void api_sound_tone_off(void){ pcspeaker_off(); }

static mez_fb_info32_t g_fb_info;
static mez_sound_info32_t g_sound_info;

static const mez_fb_info32_t* api_video_fb_get_info(void)
{
    uint32_t pitch; uint16_t width; uint16_t height; uint8_t bpp;
    const void* ptr = console_fb_get_info(&pitch, &width, &height, &bpp);
    if (!ptr) return NULL;
    g_fb_info.width = width;
    g_fb_info.height = height;
    g_fb_info.pitch = pitch;
    g_fb_info.bpp = bpp;
    g_fb_info.framebuffer = ptr;
    g_fb_info.reserved0 = g_fb_info.reserved1 = g_fb_info.reserved2 = 0;
    return &g_fb_info;
}

static void api_video_fb_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color)
{
    uint32_t pitch; uint16_t fb_w; uint16_t fb_h; uint8_t bpp;
    const void* ptr = console_fb_get_info(&pitch, &fb_w, &fb_h, &bpp);
    if (!ptr || bpp != 8) return;
    if (x >= fb_w || y >= fb_h) return;

    if ((uint32_t)x + width > fb_w) {
        width = (uint16_t)(fb_w - x);
    }
    if ((uint32_t)y + height > fb_h) {
        height = (uint16_t)(fb_h - y);
    }
    if (width == 0 || height == 0) return;

    if (!fb_accel_fill_rect(x, y, width, height, color)) {
        volatile uint8_t* fb = (volatile uint8_t*)(uintptr_t)ptr;
        for (uint16_t yy = 0; yy < height; yy++) {
            volatile uint8_t* line = fb + ((uint32_t)(y + yy) * pitch) + x;
            for (uint16_t xx = 0; xx < width; xx++) {
                line[xx] = color;
            }
        }
    }
}

static const mez_sound_info32_t* api_sound_get_info(void)
{
    g_sound_info.backends = MEZ_SOUND_BACKEND_NONE;
    g_sound_info.sb16_base_port = 0;
    g_sound_info.sb16_irq = 0;
    g_sound_info.sb16_dma8 = 0;
    g_sound_info.sb16_dma16 = 0;
    g_sound_info.sb16_version_major = 0;
    g_sound_info.sb16_version_minor = 0;
    g_sound_info.reserved0 = 0;
    g_sound_info.reserved1 = 0;

    if (pcspeaker_present()) {
        g_sound_info.backends |= MEZ_SOUND_BACKEND_PCSPK;
    }

    if (sb16_present()) {
        const sb16_info_t* info = sb16_get_info();
        if (info) {
            g_sound_info.backends |= MEZ_SOUND_BACKEND_SB16;
            g_sound_info.sb16_base_port = info->base_port;
            g_sound_info.sb16_irq = info->irq;
            g_sound_info.sb16_dma8 = info->dma8;
            g_sound_info.sb16_dma16 = info->dma16;
            g_sound_info.sb16_version_major = info->version_major;
            g_sound_info.sb16_version_minor = info->version_minor;
        }
    }

    return &g_sound_info;
}

static mez_api32_t g_api = {
    .abi_version     = MEZ_ABI32_V1,
    .size            = sizeof(mez_api32_t),
    .arch            = MEZ_ARCH_X86_32,

    .console_write   = console_write,
    .console_writeln = console_writeln,
    .console_clear   = console_clear,

    .input_poll_key  = keyboard_poll_char,

    .time_ticks_get  = platform_ticks_get,
    .time_timer_hz   = platform_timer_get_hz,
    .time_sleep_ms   = api_time_sleep_ms,

    .sound_beep      = pcspeaker_beep,
    .sound_tone_on   = api_sound_tone_on,
    .sound_tone_off  = api_sound_tone_off,

    .text_put        = api_text_put,
    .text_fill_line  = api_text_fill_line,
    .status_left     = console_status_set_left,
    .status_right    = console_draw_status_right,

    .capabilities    = 0,
    .video_fb_get_info = api_video_fb_get_info,
    .video_fb_fill_rect = api_video_fb_fill_rect,

    .sound_get_info    = api_sound_get_info,
};

const mez_api32_t* mez_api_get(void)
{
    uint32_t pitch; uint16_t width, height; uint8_t bpp;
    const void* fb = console_fb_get_info(&pitch, &width, &height, &bpp);
    uint32_t caps = 0;
    if (fb) {
        caps |= MEZ_CAP_VIDEO_FB;
        if (fb_accel_available()) {
            caps |= MEZ_CAP_VIDEO_FB_ACCEL;
        }
    }
    if (sb16_present()) {
        caps |= MEZ_CAP_SOUND_SB16;
    }
    g_api.capabilities = caps;
    return &g_api;
}
