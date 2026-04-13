#include "smos.h"
#include "vga_hw.h"
#include "fb_accel.h"
#include "../../console.h"
#include "../../config.h"
#include "../../interrupts.h"
#include "../../memory.h"
#include <stddef.h>
#include <stdint.h>

#if CONFIG_ARCH_X86
#include "../../arch/x86/io.h"

// Dynamically allocated to avoid clobbering low memory in BSS
static uint8_t* g_smos_shadow = NULL;
static int g_smos_dirty = 0;

typedef struct {
    uint8_t* buffer;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
} smos_fb_state_t;

static smos_fb_state_t g_smos_fb;
static volatile uint8_t* g_smos_vram_window = (volatile uint8_t*)0xA0000;

static void smos_copy_string(char* dst, uint32_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    uint32_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1u < dst_len && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void smos_shadow_upload(void) {
    if (!g_smos_dirty || !g_smos_shadow) return;

    const uint16_t width = 640;
    const uint16_t height = 480;
    const uint32_t pitch = 640;
    const uint32_t bytes_per_line = width / 8u;
    
    uint32_t flags = interrupts_save_disable();
    
    for (uint8_t plane = 0; plane < 4; ++plane) {
        vga_seq_write(0x02, (uint8_t)(1u << plane));
        vga_gc_write(0x04, plane);

        for (uint16_t y = 0; y < height; ++y) {
            const uint8_t* src_line = g_smos_shadow + (uint32_t)y * pitch;
            volatile uint8_t* dst_line = g_smos_vram_window + (uint32_t)y * bytes_per_line;
            for (uint32_t byte_index = 0; byte_index < bytes_per_line; ++byte_index) {
                uint8_t packed = 0;
                const uint32_t pixel_base = byte_index << 3;
                
                if (src_line[pixel_base + 0] & (1u << plane)) packed |= 0x80;
                if (src_line[pixel_base + 1] & (1u << plane)) packed |= 0x40;
                if (src_line[pixel_base + 2] & (1u << plane)) packed |= 0x20;
                if (src_line[pixel_base + 3] & (1u << plane)) packed |= 0x10;
                if (src_line[pixel_base + 4] & (1u << plane)) packed |= 0x08;
                if (src_line[pixel_base + 5] & (1u << plane)) packed |= 0x04;
                if (src_line[pixel_base + 6] & (1u << plane)) packed |= 0x02;
                if (src_line[pixel_base + 7] & (1u << plane)) packed |= 0x01;
                
                dst_line[byte_index] = packed;
            }
        }
    }
    vga_seq_write(0x02, 0x0F);
    vga_gc_write(0x04, 0x00);
    
    g_smos_dirty = 0;
    interrupts_restore(flags);
}

static int smos_fb_fill_rect(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    if (!g_smos_shadow) return 0;
    for (uint16_t i = 0; i < height; ++i) {
        for (uint16_t j = 0; j < width; ++j) {
            g_smos_shadow[(y + i) * 640 + (x + j)] = color;
        }
    }
    g_smos_dirty = 1;
    return 1;
}

static void smos_fb_sync(void* ctx) {
    (void)ctx;
    smos_shadow_upload();
}

static void smos_fb_mark_dirty(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    (void)ctx; (void)x; (void)y; (void)width; (void)height;
    g_smos_dirty = 1;
}

static const fb_accel_ops_t g_smos_fb_ops = {
    smos_fb_fill_rect,
    smos_fb_sync,
    smos_fb_mark_dirty
};

int smos_detect(gpu_info_t* out) {
    if (!out) return 0;

    int detected_via_bios = 0;
    int detected_via_regs = 0;

    volatile char* vbios = (volatile char*)0xC0000;
    int has_compaq = 0;
    int has_8106 = 0;

    for (uint32_t i = 0; i < 0x8000; i++) {
        if (!has_compaq && vbios[i] == 'C' && vbios[i+1] == 'O' && vbios[i+2] == 'M' &&
            vbios[i+3] == 'P' && vbios[i+4] == 'A' && vbios[i+5] == 'Q') {
            has_compaq = 1;
        }
        if (!has_8106 && vbios[i] == '8' && vbios[i+1] == '1' && vbios[i+2] == '0' && vbios[i+3] == '6') {
            has_8106 = 1;
        }
        if (has_compaq && has_8106) break;
    }
    if (has_compaq) detected_via_bios = 1;

    outb(0x3DE, 0x0E);
    outb(0x3DF, 0x1A);
    outb(0x3DE, 0x08);
    uint8_t rev_primary = inb(0x3DF);
    outb(0x3DE, 0x0F);
    uint8_t rev_secondary = inb(0x3DF);

    if ((rev_primary & 0xE0) == 0xE0) {
        if (rev_secondary == 0x60 || rev_secondary == 0x61) {
            detected_via_regs = 1;
        }
    }

    if (!detected_via_bios && !detected_via_regs) return 0;

    out->type = GPU_TYPE_VGA;
    if (detected_via_regs) {
        if (rev_secondary == 0x61) {
            smos_copy_string(out->name, sizeof(out->name), "SMOS SPC8106F0B (Aero)");
        } else {
            smos_copy_string(out->name, sizeof(out->name), "SMOS SPC8106F0A");
        }
    } else {
        smos_copy_string(out->name, sizeof(out->name), "Compaq SPC8106 (Aero)");
    }

    out->framebuffer_base = 0xA0000;
    out->framebuffer_size = 256 * 1024;
    out->capabilities = GPU_CAP_LINEAR_FB;

    return 1;
}

int smos_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp) {
    uint16_t target_w = width;
    uint16_t target_h = height;

    if (bpp == 8) {
        if (width == 640 && height == 400) {
            vga_set_mode_640x400x256();
            target_w = 640;
            target_h = 400;
        } else {
            vga_set_mode13();
            target_w = 320;
            target_h = 200;
        }
    }
 else if (bpp == 4) {
        // Plane mode (Mode 12h) - already set by BIOS or handled via sync
        target_w = 640;
        target_h = 480;
    }

    if (!g_smos_shadow) {
        g_smos_shadow = (uint8_t*)memory_alloc(640 * 480);
        if (!g_smos_shadow) return 0;
    }

    for (uint32_t i = 0; i < 640*480; i++) g_smos_shadow[i] = 0;

    g_smos_fb.buffer = g_smos_shadow;
    g_smos_fb.width = target_w;
    g_smos_fb.height = target_h;
    g_smos_fb.pitch = target_w;
    g_smos_fb.bpp = bpp;
    
    fb_accel_register(&g_smos_fb_ops, &g_smos_fb);

    gpu->framebuffer_width = target_w;
    gpu->framebuffer_height = target_h;
    gpu->framebuffer_pitch = target_w;
    gpu->framebuffer_bpp = bpp;
    gpu->framebuffer_ptr = g_smos_shadow;
    
    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->pixel_format = (bpp == 8) ? DISPLAY_PIXEL_FORMAT_PAL_256 : DISPLAY_PIXEL_FORMAT_PAL_16;
    out_mode->width = target_w;
    out_mode->height = target_h;
    out_mode->bpp = bpp;
    out_mode->pitch = target_w;
    out_mode->phys_base = 0xA0000;
    out_mode->framebuffer = g_smos_shadow;
    
    // Explicitly set driver name for the display manager
    for (int i=0; i<32; i++) out_mode->driver_name[i] = gpu->name[i];

    vga_dac_load_default_palette();

    return 1;
}

void smos_restore_text_mode(void) {
    fb_accel_reset();
    
    // Force standard VGA text mode bits
    vga_gc_write(0x06, 0x00); // Miscellaneous: Graphics Mode = 0
    vga_seq_write(0x04, 0x02); // Memory Mode: Chain-4 off, Odd/Even on
    vga_attr_write(0x10, 0x01); // Mode Control: Graphics bit = 0
    
    // Clear VRAM for text
    volatile uint16_t* text_vram = (volatile uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) text_vram[i] = 0x0720;
}

#endif
