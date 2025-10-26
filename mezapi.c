#include "mezapi.h"
#include "console.h"
#include "keyboard.h"
#include "platform.h"
#include "drivers/pcspeaker.h"
#include "drivers/sb16.h"
#include "drivers/gpu/fb_accel.h"
#include "drivers/gpu/gpu.h"
#include "video_fb.h"
#include <stddef.h>
#include <stdint.h>
#include "statusbar.h"

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
static mez_gpu_info32_t g_gpu_info;

static void mez_copy_string(char* dst, size_t len, const char* src)
{
    if (!dst || len == 0) {
        return;
    }
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

static uint32_t mez_gpu_map_caps(const gpu_info_t* gpu)
{
    uint32_t caps = 0;
    if (!gpu) {
        return 0;
    }
    if (gpu->capabilities & GPU_CAP_LINEAR_FB) {
        caps |= MEZ_GPU_CAP_LINEAR_FB;
    }
    if (gpu->capabilities & GPU_CAP_ACCEL_2D) {
        caps |= MEZ_GPU_CAP_ACCEL_2D;
    }
    if (gpu->capabilities & GPU_CAP_HW_CURSOR) {
        caps |= MEZ_GPU_CAP_HW_CURSOR;
    }
    if (gpu->capabilities & GPU_CAP_VBE_BIOS) {
        caps |= MEZ_GPU_CAP_VBE_BIOS;
    }
    switch (gpu->type) {
        case GPU_TYPE_ET4000:
        case GPU_TYPE_ET4000AX:
        case GPU_TYPE_AVGA2:
        case GPU_TYPE_VGA:
            caps |= MEZ_GPU_CAP_BANKED_FB;
            break;
        default:
            break;
    }
    return caps;
}

static mez_gpu_feature_level_t mez_gpu_classify(const gpu_info_t* gpu)
{
    if (!gpu) {
        return MEZ_GPU_FEATURELEVEL_TEXTMODE;
    }
    switch (gpu->type) {
        case GPU_TYPE_CIRRUS:
            if (gpu->capabilities & GPU_CAP_ACCEL_2D) {
                return MEZ_GPU_FEATURELEVEL_LINEAR_FB_ACCEL;
            }
            return MEZ_GPU_FEATURELEVEL_LINEAR_FB;
        case GPU_TYPE_ET4000AX:
            return MEZ_GPU_FEATURELEVEL_BANKED_FB_ACCEL;
        case GPU_TYPE_ET4000:
        case GPU_TYPE_AVGA2:
            return MEZ_GPU_FEATURELEVEL_BANKED_FB;
        default:
            return MEZ_GPU_FEATURELEVEL_TEXTMODE;
    }
}

static uint32_t mez_gpu_map_adapter(const gpu_info_t* gpu)
{
    if (!gpu) {
        return MEZ_GPU_ADAPTER_TEXTMODE;
    }
    switch (gpu->type) {
        case GPU_TYPE_CIRRUS:
            return MEZ_GPU_ADAPTER_CIRRUS;
        case GPU_TYPE_ET4000:
            return MEZ_GPU_ADAPTER_TSENG_ET4000;
        case GPU_TYPE_ET4000AX:
            return MEZ_GPU_ADAPTER_TSENG_ET4000AX;
        case GPU_TYPE_AVGA2:
            return MEZ_GPU_ADAPTER_ACUMOS_AVGA2;
        default:
            return MEZ_GPU_ADAPTER_TEXTMODE;
    }
}

static const char* mez_gpu_default_name(uint32_t adapter_type)
{
    switch (adapter_type) {
        case MEZ_GPU_ADAPTER_CIRRUS:
            return "Cirrus Logic";
        case MEZ_GPU_ADAPTER_TSENG_ET4000:
            return "Tseng ET4000";
        case MEZ_GPU_ADAPTER_TSENG_ET4000AX:
            return "Tseng ET4000AX";
        case MEZ_GPU_ADAPTER_ACUMOS_AVGA2:
            return "Acumos AVGA2";
        case MEZ_GPU_ADAPTER_TEXTMODE:
            return "VGA Text-Mode";
        default:
            return "Unknown GPU";
    }
}

static void mez_gpu_info_reset(mez_gpu_info32_t* info)
{
    if (!info) {
        return;
    }
    info->feature_level = MEZ_GPU_FEATURELEVEL_TEXTMODE;
    info->adapter_type = MEZ_GPU_ADAPTER_TEXTMODE;
    info->capabilities = 0;
    info->vram_bytes = 0;
    info->name[0] = '\0';
}

static const mez_gpu_info32_t* api_video_gpu_get_info(void)
{
    size_t count = 0;
    const gpu_info_t* devices = gpu_get_devices(&count);
    const gpu_info_t* best_gpu = NULL;
    mez_gpu_feature_level_t best_level = MEZ_GPU_FEATURELEVEL_TEXTMODE;
    uint32_t best_caps = 0;
    uint32_t best_vram = 0;

    if (devices && count) {
        for (size_t i = 0; i < count; ++i) {
            const gpu_info_t* gpu = &devices[i];
            mez_gpu_feature_level_t level = mez_gpu_classify(gpu);
            uint32_t caps = mez_gpu_map_caps(gpu);
            uint32_t vram = gpu->framebuffer_size;
            if (!best_gpu || level > best_level ||
                (level == best_level && caps > best_caps) ||
                (level == best_level && caps == best_caps && vram > best_vram)) {
                best_gpu = gpu;
                best_level = level;
                best_caps = caps;
                best_vram = vram;
            }
        }
    }

    mez_gpu_info_reset(&g_gpu_info);

    if (best_gpu) {
        uint32_t adapter = mez_gpu_map_adapter(best_gpu);
        g_gpu_info.feature_level = best_level;
        g_gpu_info.adapter_type = adapter;
        g_gpu_info.capabilities = best_caps;
        g_gpu_info.vram_bytes = best_vram;
        if (best_gpu->name[0]) {
            mez_copy_string(g_gpu_info.name, sizeof(g_gpu_info.name), best_gpu->name);
        } else {
            mez_copy_string(g_gpu_info.name, sizeof(g_gpu_info.name), mez_gpu_default_name(adapter));
        }
    } else {
        mez_copy_string(g_gpu_info.name, sizeof(g_gpu_info.name), mez_gpu_default_name(MEZ_GPU_ADAPTER_TEXTMODE));
    }

    return &g_gpu_info;
}

static statusbar_pos_t mez_pos_to_statusbar(mez_status_pos_t pos) {
    switch (pos) {
        case MEZ_STATUS_POS_LEFT: return STATUSBAR_POS_LEFT;
        case MEZ_STATUS_POS_CENTER: return STATUSBAR_POS_CENTER;
        case MEZ_STATUS_POS_RIGHT: return STATUSBAR_POS_RIGHT;
        default: return STATUSBAR_POS_LEFT;
    }
}

static mez_status_slot_t api_status_register(mez_status_pos_t pos, uint8_t priority, uint8_t flags, char icon, const char* initial_text) {
    statusbar_slot_desc_t desc = {
        .position = mez_pos_to_statusbar(pos),
        .priority = priority,
        .flags = (flags & MEZ_STATUS_FLAG_ICON_ONLY_ON_TRUNCATE) ? STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE : 0,
        .icon = icon,
        .initial_text = initial_text
    };
    statusbar_slot_t s = statusbar_register(&desc);
    return (s == STATUSBAR_SLOT_INVALID) ? MEZ_STATUS_SLOT_INVALID : (mez_status_slot_t)s;
}

static void api_status_update(mez_status_slot_t slot, const char* text) {
    if (slot == MEZ_STATUS_SLOT_INVALID) return;
    statusbar_set_text((statusbar_slot_t)slot, text);
}

static void api_status_release(mez_status_slot_t slot) {
    if (slot == MEZ_STATUS_SLOT_INVALID) return;
    statusbar_release((statusbar_slot_t)slot);
}

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

    .status_register = api_status_register,
    .status_update   = api_status_update,
    .status_release  = api_status_release,

    .capabilities    = 0,
    .video_fb_get_info = api_video_fb_get_info,
    .video_fb_fill_rect = api_video_fb_fill_rect,

    .sound_get_info    = api_sound_get_info,
    .video_gpu_get_info = api_video_gpu_get_info,
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
    if (api_video_gpu_get_info()) {
        caps |= MEZ_CAP_VIDEO_GPU_INFO;
    }
    g_api.capabilities = caps;
    return &g_api;
}
