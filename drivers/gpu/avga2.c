#include "avga2.h"
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

// Bank switching for Legacy Cirrus (CL-GD5402 / Acumos AVGA2)
static void avga2_set_bank(uint8_t bank) {
    // GR09 bits 4-7: Bank selection
    vga_gc_write(0x09, (uint8_t)(bank << 4));
}

static void avga2_unlock(void) {
    vga_seq_write(0x06, 0x12);
}

static void avga2_clear_vram(void) {
    avga2_unlock();
    for (uint8_t bank = 0; bank < 8; bank++) {
        avga2_set_bank(bank);
        volatile uint32_t* vram = (volatile uint32_t*)0xA0000;
        for (uint32_t i = 0; i < 16384; i++) vram[i] = 0;
    }
}

static uint8_t* g_avga2_shadow = NULL;
static uint16_t g_avga2_w, g_avga2_h;
static uint8_t  g_avga2_bpp;
static uint32_t g_avga2_dirty_mask[15]; 
static uint8_t  g_avga2_current_bank = 0xFF;

static void avga2_fb_mark_dirty(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    (void)ctx; (void)x; (void)width;
    for (uint16_t i = 0; i < height; i++) {
        uint16_t line = y + i;
        if (line < 480) {
            g_avga2_dirty_mask[line >> 5] |= (1u << (line & 31));
        }
    }
}

static void avga2_fb_sync(void* ctx) {
    (void)ctx;
    if (!g_avga2_shadow) return;

    uint32_t flags = interrupts_save_disable();
    
    if (g_avga2_bpp == 8) {
        for (uint16_t y = 0; y < g_avga2_h; y++) {
            if (!(g_avga2_dirty_mask[y >> 5] & (1u << (y & 31)))) continue;

            uint32_t line_offset = (uint32_t)y * g_avga2_w;
            uint8_t bank = (uint8_t)(line_offset >> 16);
            uint16_t bank_off = (uint16_t)(line_offset & 0xFFFF);
            
            if (bank != g_avga2_current_bank) {
                avga2_set_bank(bank);
                g_avga2_current_bank = bank;
            }

            volatile uint8_t* vram = (volatile uint8_t*)0xA0000;
            
            if (bank_off + g_avga2_w <= 65536) {
                const uint32_t* src32 = (const uint32_t*)(g_avga2_shadow + line_offset);
                volatile uint32_t* dst32 = (volatile uint32_t*)(vram + bank_off);
                for (uint16_t i = 0; i < 160; i++) {
                    dst32[i] = src32[i];
                }
            } else {
                for (uint16_t i = 0; i < g_avga2_w; i++) {
                    uint32_t abs_off = line_offset + i;
                    uint8_t b = (uint8_t)(abs_off >> 16);
                    if (b != g_avga2_current_bank) {
                        avga2_set_bank(b);
                        g_avga2_current_bank = b;
                    }
                    vram[abs_off & 0xFFFF] = g_avga2_shadow[abs_off];
                }
            }
            g_avga2_dirty_mask[y >> 5] &= ~(1u << (y & 31));
        }
    } else if (g_avga2_bpp == 4) {
        const uint32_t bytes_per_plane_line = g_avga2_w / 8;
        for (uint8_t plane = 0; plane < 4; plane++) {
            vga_seq_write(0x02, (uint8_t)(1u << plane));
            vga_gc_write(0x04, plane);
            for (uint16_t y = 0; y < g_avga2_h; y++) {
                if (!(g_avga2_dirty_mask[y >> 5] & (1u << (y & 31)))) continue;
                const uint8_t* src_line = g_avga2_shadow + (uint32_t)y * g_avga2_w;
                volatile uint8_t* dst_line = (volatile uint8_t*)0xA0000 + (uint32_t)y * bytes_per_plane_line;
                for (uint32_t b = 0; b < bytes_per_plane_line; b++) {
                    uint8_t packed = 0;
                    uint32_t px = b << 3;
                    for (int i=0; i<8; i++) {
                        if (src_line[px + i] & (1u << plane)) packed |= (uint8_t)(0x80 >> i);
                    }
                    dst_line[b] = packed;
                }
            }
        }
        for (int i=0; i<15; i++) g_avga2_dirty_mask[i] = 0;
        vga_seq_write(0x02, 0x0F);
        vga_gc_write(0x04, 0x00);
    }

    interrupts_restore(flags);
}

static int avga2_fb_fill_rect(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    (void)ctx;
    if (!g_avga2_shadow) return 0;
    for (uint16_t i = 0; i < height; i++) {
        for (uint16_t j = 0; j < width; j++) {
            if ((y+i) < g_avga2_h && (x+j) < g_avga2_w)
                g_avga2_shadow[(y + i) * g_avga2_w + (x + j)] = color;
        }
    }
    avga2_fb_mark_dirty(NULL, x, y, width, height);
    return 1;
}

static const fb_accel_ops_t g_avga2_ops = {
    avga2_fb_fill_rect,
    avga2_fb_sync,
    avga2_fb_mark_dirty
};

int avga2_signature_present(void) {
    const volatile uint8_t* bios = (const volatile uint8_t*)0xC0000;
    const char* sig = "ACUMOS";
    for (uint32_t i = 0; i < 0x8000; i++) {
        if (bios[i] == 'A' && bios[i+1] == 'C') {
            int match = 1;
            for (int j = 0; j < 6; j++) { if (bios[i+j] != sig[j]) { match = 0; break; } }
            if (match) return 1;
        }
    }
    uint8_t old = vga_seq_read(0x06);
    vga_seq_write(0x06, 0x12);
    uint8_t val = vga_seq_read(0x06);
    vga_seq_write(0x06, old);
    return (val == 0x12);
}

static uint32_t avga2_get_vram_size(void) {
    avga2_unlock();
    uint8_t sr0f = vga_seq_read(0x0F);
    uint8_t mem = (uint8_t)((sr0f >> 3) & 0x03);
    uint32_t size = 256 * 1024;
    if (mem == 1) size = 512 * 1024;
    else if (mem == 2) size = 1024 * 1024;
    else if (mem > 2) size = 2048 * 1024;

    if (size <= 256 * 1024) return size;

    // Destructive Wraparound Check: Verify if 512KB is physically present
    uint32_t flags = interrupts_save_disable();
    avga2_set_bank(0);
    volatile uint8_t* vram = (volatile uint8_t*)0xA0000;
    uint8_t old0 = vram[0];
    avga2_set_bank(4); // 4 * 64KB = 256KB offset
    uint8_t old4 = vram[0];

    vram[0] = 0xAA;
    avga2_set_bank(0);
    vram[0] = 0x55;
    avga2_set_bank(4);
    uint8_t check = vram[0];

    // Restore original values
    vram[0] = old4;
    avga2_set_bank(0);
    vram[0] = old0;
    interrupts_restore(flags);

    if (check == 0x55) {
        // Wraparound detected! The hardware lied.
        return 256 * 1024;
    }
    return size;
}

void avga2_classify_info(gpu_info_t* info) {
    if (!info) return;
    if (avga2_signature_present()) {
        info->type = GPU_TYPE_AVGA2;
        for(int i=0; i<32; i++) info->name[i] = 0;
        const char* name = "Acumos AVGA2 (Cirrus ISA)";
        for(int i=0; name[i] && i < 31; i++) info->name[i] = name[i];
        info->framebuffer_base = 0xA0000;
        info->framebuffer_bar = 0xFF;
        info->framebuffer_size = avga2_get_vram_size();
        info->capabilities = 0;
    }
}

int avga2_restore_text_mode(void) {
    fb_accel_reset();
    uint32_t irq_flags = interrupts_save_disable();
    avga2_unlock();
    vga_seq_write(0x07, 0x00); 
    vga_gc_write(0x09, 0x00);
    vga_set_mode3();
    vga_seq_write(0x06, 0x00);
    interrupts_restore(irq_flags);
    return 1;
}

int avga2_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp) {
    uint32_t vram = avga2_get_vram_size();
    uint32_t needed = (uint32_t)width * (uint32_t)height * (uint32_t)bpp / 8;

    if (needed > vram) {
        if (width == 640 && height == 480 && bpp == 8) {
            bpp = 4;
            console_writeln("AVGA2: 640x480x8 too large, falling back to 640x480x16 (4bpp)");
        } else {
            width = 320; height = 200; bpp = 8;
            console_writeln("AVGA2: Falling back to 320x200x8");
        }
    }

    if (!g_avga2_shadow) {
        g_avga2_shadow = (uint8_t*)memory_alloc(640 * 480);
        if (!g_avga2_shadow) return 0;
    }
    for (uint32_t i = 0; i < 640 * 480; i++) g_avga2_shadow[i] = 0;
    for (int i=0; i<15; i++) g_avga2_dirty_mask[i] = 0xFFFFFFFF;
    
    g_avga2_w = width;
    g_avga2_h = height;
    g_avga2_bpp = bpp;

    uint32_t irq_flags = interrupts_save_disable();
    if (width == 320 && height == 200 && bpp == 8) {
        vga_set_mode13();
        out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    } else if (width == 640 && height == 480 && bpp == 4) {
        vga_misc_write(0xE3);
        vga_seq_write(0x00, 0x03);
        vga_seq_write(0x01, 0x01);
        vga_seq_write(0x02, 0x0F);
        vga_seq_write(0x03, 0x00);
        vga_seq_write(0x04, 0x06);
        uint8_t crt11 = vga_crtc_read(0x11);
        vga_crtc_write(0x11, crt11 & 0x7F);
        static const uint8_t crtc12[25] = {
            0x5F,0x4F,0x50,0x82,0x54,0x80,0x0B,0x3E,
            0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
            0xEA,0x8C,0xDF,0x28,0x00,0xE7,0x04,0xE3,0xFF
        };
        for (int i=0; i<25; i++) vga_crtc_write(i, crtc12[i]);
        vga_gc_write(0x05, 0x00);
        vga_gc_write(0x06, 0x05);
        out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_16;
    } else if (width == 640 && bpp == 8) {
        vga_set_mode13(); 
        avga2_unlock();
        vga_misc_write(0xE3);
        uint8_t crt11 = vga_crtc_read(0x11);
        vga_crtc_write(0x11, crt11 & 0x7F);
        vga_crtc_write(0x00, 0x5F);
        vga_crtc_write(0x01, 0x4F);
        vga_crtc_write(0x06, 0x0B);
        vga_crtc_write(0x07, 0x3E);
        vga_crtc_write(0x09, 0x40);
        vga_crtc_write(0x10, 0xEA);
        vga_crtc_write(0x11, 0x8C);
        if (height == 480) vga_crtc_write(0x12, 0xDF);
        else vga_crtc_write(0x12, 0x8F);
        vga_crtc_write(0x13, 0x50);
        vga_crtc_write(0x17, 0xC3);
        vga_seq_write(0x07, 0x01);
        out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    } else {
        interrupts_restore(irq_flags);
        return 0;
    }

    avga2_clear_vram();
    fb_accel_register(&g_avga2_ops, NULL);

    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->width = width;
    out_mode->height = height;
    out_mode->bpp = bpp;
    out_mode->pitch = width; 
    out_mode->phys_base = 0xA0000;
    out_mode->framebuffer = g_avga2_shadow;
    out_mode->set_bank = NULL; 

    if (gpu) {
        gpu->framebuffer_bpp = bpp;
        gpu->framebuffer_width = width;
        gpu->framebuffer_height = height;
    }

    interrupts_restore(irq_flags);
    return 1;
}

void avga2_dump_state(void) {
    console_writeln("Acumos AVGA2 (ISA) State Dump:");
    avga2_unlock();
    console_write("SR06 (Unlock): "); console_write_hex16(vga_seq_read(0x06)); console_writeln("");
    console_write("SR07 (ExtMode): "); console_write_hex16(vga_seq_read(0x07)); console_writeln("");
    console_write("GR09 (Bank): "); console_write_hex16(vga_gc_read(0x09)); console_writeln("");
}

#endif
