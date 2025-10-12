#include "et4000.h"
#include "vga_hw.h"
#include "fb_accel.h"
#include "et4000_common.h"
#include "../../config.h"
#if CONFIG_ARCH_X86
#include "../../arch/x86/io.h"
#include "et4000_ax_detect.h"
#endif

// Globale Variable für AX-Variante
int g_is_ax_variant = 0;

#include <stddef.h>
#include <stdint.h>

#if !CONFIG_ARCH_X86
int et4000_detect(gpu_info_t* out_info) {
    (void)out_info;
    return 0;  // Nicht-x86 Plattform
}

int et4000_set_mode_640x480x8(gpu_info_t* gpu, display_mode_info_t* out_mode) {
    (void)gpu;
    (void)out_mode;
    return 0;  // Nicht-x86 Plattform
}

void et4000_restore_text_mode(void) {
    // Nicht-x86 Plattform
}
#else

#include "et4000ax.h"
#include "et4000_ax_detect.h"

#define ET4K_SEGMENT_PORT 0x3CD
#define ET4K_BANK_PORT    0x3CB
#define ET4K_WINDOW_PHYS  0xA0000u
#define ET4K_BANK_SIZE    0x10000u

static uint8_t g_et4k_shadow[640u * 480u];

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t* shadow;
    uint32_t shadow_size;
    uint8_t  dirty_first_bank;
    uint8_t  dirty_last_bank;
    uint8_t  dirty_pending;
} et4k_state_t;

static et4k_state_t g_et4k = {0};

// Standard VGA Register-Werte
static const uint8_t std_seq_text[5] = { 0x03,0x00,0x03,0x00,0x02 };
static const uint8_t std_crtc_text[25] = {
    0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,
    0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x50,
    0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,
    0xFF
};
static const uint8_t std_graph_text[9] = { 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF };
static const uint8_t std_attr_text[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x0C,0x00,0x0F,0x08,0x00
};

static const uint8_t std_seq_640x480[5] = { 0x03,0x01,0x0F,0x00,0x06 };
static const uint8_t std_crtc_640x480[25] = {
    0x5F,0x4F,0x50,0x82,0x54,0x80,0x0B,0x3E,
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
    0xEA,0x0C,0xDF,0x28,0x00,0xE7,0x04,0xE3,
    0xFF
};
static const uint8_t std_graph_640x480[9] = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
static const uint8_t std_attr_640x480[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x41,0x00,0x0F,0x00,0x00
};

static void et4k_memset8(void* dst, uint8_t value, size_t count) {
    uint8_t* ptr = (uint8_t*)dst;
    for (size_t i = 0; i < count; i++) {
        ptr[i] = value;
    }
}

static void et4k_bzero(void* dst, size_t count) {
    et4k_memset8(dst, 0u, count);
}

static void et4k_copy_name(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    size_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1 < dst_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void vga_program_standard_mode(uint8_t misc,
                                      const uint8_t seq[5],
                                      const uint8_t crtc[25],
                                      const uint8_t graph[9],
                                      const uint8_t attr[21]) {
    vga_misc_write(misc);
    for (int i = 0; i < 5; i++) {
        vga_seq_write((uint8_t)i, seq[i]);
    }
    vga_crtc_write(0x11, 0x0E);
    for (int i = 0; i < 25; i++) {
        vga_crtc_write((uint8_t)i, crtc[i]);
    }
    for (int i = 0; i < 9; i++) {
        vga_gc_write((uint8_t)i, graph[i]);
    }
    for (int i = 0; i < 21; i++) {
        vga_attr_write((uint8_t)i, attr[i]);
    }
    vga_attr_reenable_video();
}

static void et4k_select_segment(uint8_t segment) {
    outb(ET4K_BANK_PORT, (uint8_t)(((segment & 0x30u) | (segment >> 4)) & 0x3Fu));
    outb(ET4K_SEGMENT_PORT, (uint8_t)((segment & 0x0Fu) | ((segment & 0x0Fu) << 4)));
}

static void et4k_mark_dirty_internal(uint32_t start_offset, uint32_t end_offset) {
    uint8_t first_bank = (uint8_t)(start_offset / ET4K_BANK_SIZE);
    uint8_t last_bank = (uint8_t)(end_offset / ET4K_BANK_SIZE);
    if (!g_et4k.dirty_pending) {
        g_et4k.dirty_first_bank = first_bank;
        g_et4k.dirty_last_bank = last_bank;
        g_et4k.dirty_pending = 1;
    } else {
        if (first_bank < g_et4k.dirty_first_bank) {
            g_et4k.dirty_first_bank = first_bank;
        }
        if (last_bank > g_et4k.dirty_last_bank) {
            g_et4k.dirty_last_bank = last_bank;
        }
    }
}

static void et4k_mark_dirty(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    (void)ctx;
    if (!g_et4k.shadow || g_et4k.pitch == 0 || width == 0 || height == 0) {
        return;
    }
    if (x >= g_et4k.width || y >= g_et4k.height) {
        return;
    }
    if ((uint32_t)x + (uint32_t)width > g_et4k.width) {
        width = (uint16_t)(g_et4k.width - x);
    }
    if ((uint32_t)y + (uint32_t)height > g_et4k.height) {
        height = (uint16_t)(g_et4k.height - y);
    }
    uint32_t start = (uint32_t)y * g_et4k.pitch + x;
    uint32_t end = (uint32_t)(y + height - 1u) * g_et4k.pitch + (uint32_t)(x + width - 1u);
    et4k_mark_dirty_internal(start, end);
}

static int et4k_fill_rect(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    (void)ctx;
    if (!g_et4k.shadow || g_et4k.pitch == 0) return 0;
    if (!width || !height) return 1;
    if (x >= g_et4k.width || y >= g_et4k.height) return 0;
    if ((uint32_t)x + (uint32_t)width > g_et4k.width) return 0;
    if ((uint32_t)y + (uint32_t)height > g_et4k.height) return 0;

    uint32_t pitch = g_et4k.pitch;
    for (uint16_t row = 0; row < height; row++) {
        uint8_t* dst = g_et4k.shadow + ((uint32_t)(y + row) * pitch + x);
        et4k_memset8(dst, color, width);
    }
    uint32_t start = (uint32_t)y * pitch + x;
    uint32_t end = (uint32_t)(y + height - 1u) * pitch + (uint32_t)(x + width - 1u);
    et4k_mark_dirty_internal(start, end);
    return 1;
}

static void et4k_sync(void* ctx) {
    (void)ctx;
    if (!g_et4k.dirty_pending || !g_et4k.shadow) return;
    uint8_t last = g_et4k.dirty_last_bank;
    for (uint8_t bank = g_et4k.dirty_first_bank; bank <= last; bank++) {
        et4k_select_segment(bank);
        uint32_t offset = (uint32_t)bank * ET4K_BANK_SIZE;
        if (offset >= g_et4k.shadow_size) break;
        uint32_t remaining = g_et4k.shadow_size - offset;
        uint32_t copy = (remaining >= ET4K_BANK_SIZE) ? ET4K_BANK_SIZE : remaining;
        volatile uint8_t* dst = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;
        const uint8_t* src = g_et4k.shadow + offset;
        for (uint32_t i = 0; i < copy; i++) {
            dst[i] = src[i];
        }
        if (copy < ET4K_BANK_SIZE) {
            break;
        }
    }
    g_et4k.dirty_pending = 0;
}

static const fb_accel_ops_t g_et4k_ops = {
    .fill_rect = et4k_fill_rect,
    .sync = et4k_sync,
    .mark_dirty = et4k_mark_dirty,
};

int et4000_detect(gpu_info_t* out_info) {
    if (!out_info) return 0;
    uint8_t saved_seg = inb(ET4K_SEGMENT_PORT);
    uint8_t saved_bank = inb(ET4K_BANK_PORT);

    outb(ET4K_SEGMENT_PORT, 0x55);
    uint8_t test = inb(ET4K_SEGMENT_PORT);
    if (test != 0x55) {
        outb(ET4K_SEGMENT_PORT, saved_seg);
        outb(ET4K_BANK_PORT, saved_bank);
        return 0;
    }
    outb(ET4K_SEGMENT_PORT, 0xAA);
    test = inb(ET4K_SEGMENT_PORT);

    outb(ET4K_SEGMENT_PORT, saved_seg);
    outb(ET4K_BANK_PORT, saved_bank);

    if (test != 0xAA) {
        return 0;
    }

    et4k_bzero(out_info, sizeof(*out_info));
    out_info->type = GPU_TYPE_ET4000;
    et4k_copy_name(out_info->name, sizeof(out_info->name), "Tseng ET4000");
    out_info->framebuffer_bar = 0xFF;
    out_info->framebuffer_base = ET4K_WINDOW_PHYS;
    out_info->framebuffer_size = 0;
    out_info->capabilities = 0;
    return 1;
}

static void et4k_state_reset(void) {
    g_et4k.width = 0;
    g_et4k.height = 0;
    g_et4k.pitch = 0;
    g_et4k.bpp = 0;
    g_et4k.shadow = NULL;
    g_et4k.shadow_size = 0;
    g_et4k.dirty_first_bank = 0;
    g_et4k.dirty_last_bank = 0;
    g_et4k.dirty_pending = 0;
}

int et4000_set_mode_640x480x8(gpu_info_t* gpu, display_mode_info_t* out_mode) {
    if (!gpu || !out_mode) return 0;

    vga_program_standard_mode(0xE3, std_seq_640x480, std_crtc_640x480, std_graph_640x480, std_attr_640x480);
    vga_pel_mask_write(0xFF);
    vga_dac_load_default_palette();

    et4k_bzero(g_et4k_shadow, sizeof(g_et4k_shadow));

    g_et4k.width = 640;
    g_et4k.height = 480;
    g_et4k.pitch = 640;
    g_et4k.bpp = 8;
    g_et4k.shadow = g_et4k_shadow;
    g_et4k.shadow_size = (uint32_t)g_et4k.pitch * g_et4k.height;
    g_et4k.dirty_first_bank = 0;
    g_et4k.dirty_last_bank = (uint8_t)((g_et4k.shadow_size - 1u) / ET4K_BANK_SIZE);
    g_et4k.dirty_pending = 1;

    fb_accel_register(&g_et4k_ops, &g_et4k);

    gpu->framebuffer_width = g_et4k.width;
    gpu->framebuffer_height = g_et4k.height;
    gpu->framebuffer_pitch = g_et4k.pitch;
    gpu->framebuffer_bpp = g_et4k.bpp;
    gpu->framebuffer_ptr = g_et4k.shadow;
    gpu->framebuffer_size = g_et4k.shadow_size;

    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    out_mode->width = g_et4k.width;
    out_mode->height = g_et4k.height;
    out_mode->bpp = g_et4k.bpp;
    out_mode->pitch = g_et4k.pitch;
    out_mode->phys_base = gpu->framebuffer_base;
    out_mode->framebuffer = g_et4k.shadow;

    et4k_sync(&g_et4k);
    return 1;
}

void et4000_restore_text_mode(void) {
    fb_accel_reset();
    vga_program_standard_mode(0x67, std_seq_text, std_crtc_text, std_graph_text, std_attr_text);
    vga_dac_reset_text_palette();
    et4k_state_reset();
}

#endif // !CONFIG_ARCH_X86
