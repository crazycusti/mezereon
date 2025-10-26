#include "cirrus.h"
#include "../pci.h"
#include "vga_hw.h"
#include "../../console.h"
#include "../../interrupts.h"
#include <stddef.h>
#include <stdint.h>

#define CIRRUS_VENDOR_ID 0x1013
#define CIRRUS_DEVICE_ID_GD5446 0x00B8

static void copy_name(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    size_t i = 0;
    while (i + 1 < dst_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void print_byte_hex(uint8_t v) {
    const char* hex = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[v & 0xF];
    buf[2] = '\0';
    console_write(buf);
}

static void dump_block(const char* title, uint8_t (*reader)(uint8_t), const uint8_t* indices, size_t count) {
    if (!reader || !indices || count == 0) return;
    console_write("      ");
    console_writeln(title);
    for (size_t idx = 0; idx < count; idx++) {
        uint8_t reg = indices[idx];
        console_write("        ");
        console_write(title[0] == 'S' ? "SR" : (title[0] == 'C' ? "CR" : (title[0] == 'G' ? "GR" : "AR")));
        print_byte_hex(reg);
        console_write("=");
        print_byte_hex(reader(reg));
        if ((idx & 0x03) == 0x03 || idx + 1 == count) console_writeln("");
        else console_write("  ");
    }
    if ((count & 0x03) != 0) console_writeln("");
}

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

static const uint16_t cirrus_seq_text[] = { 0x0007, 0xFFFF };
static const uint16_t cirrus_graph_text[] = { 0x0009, 0x000A, 0x000B, 0xFFFF };
static const uint16_t cirrus_crtc_text[] = { 0x001A, 0x001B, 0x001D, 0xFFFF };

static const uint16_t cirrus_seq_640x480x8[] = {
    0x0300,0x2101,0x0F02,0x0003,0x0E04,0x1107,
    0x580B,0x580C,0x580D,0x580E,
    0x0412,0x0013,0x2017,
    0x331B,0x331C,0x331D,0x331E,
    0xFFFF
};

static const uint16_t cirrus_seq_640x480x16[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,0x580b,0x580c,0x580d,0x580e,0x0412,0x0013,0x2017,0x331b,0x331c,0x331d,0x331e,0xffff };
static const uint16_t cirrus_seq_640x480x24[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,0x580b,0x580c,0x580d,0x580e,0x0412,0x0013,0x2017,0x331b,0x331c,0x331d,0x331e,0xffff };
static const uint16_t cirrus_seq_800x600x8[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,0x230b,0x230c,0x230d,0x230e,0x0412,0x0013,0x2017,0x141b,0x141c,0x141d,0x141e,0xffff };
static const uint16_t cirrus_seq_800x600x16[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,0x230b,0x230c,0x230d,0x230e,0x0412,0x0013,0x2017,0x141b,0x141c,0x141d,0x141e,0xffff };
static const uint16_t cirrus_seq_800x600x24[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,0x230b,0x230c,0x230d,0x230e,0x0412,0x0013,0x2017,0x141b,0x141c,0x141d,0x141e,0xffff };
static const uint16_t cirrus_seq_1024x768x8[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };
static const uint16_t cirrus_seq_1024x768x16[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };
static const uint16_t cirrus_seq_1024x768x24[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };
static const uint16_t cirrus_seq_1280x1024x8[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };
static const uint16_t cirrus_seq_1280x1024x16[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };
static const uint16_t cirrus_seq_1600x1200x8[] = { 0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,0x760b,0x760c,0x760d,0x760e,0x0412,0x0013,0x2017,0x341b,0x341c,0x341d,0x341e,0xffff };

static const uint16_t cirrus_graph_svgacolor[] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x4005,0x0506,0x0F07,0xFF08,
    0x0009,0x000A,0x000B,
    0xFFFF
};

static const uint16_t cirrus_crtc_640x480x8[] = {
    0x2C11,
    0x5F00,0x4F01,0x4F02,0x8003,0x5204,0x1E05,0x0B06,0x3E07,
    0x4009,0x000C,0x000D,
    0xEA10,0xDF12,0x5013,0x4014,0xDF15,0x0B16,0xC317,0xFF18,
    0x001A,0x221B,0x001D,
    0xFFFF
};
static const uint16_t cirrus_crtc_640x480x16[] = { 0x2c11,0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,0x4009,0x000c,0x000d,0xea10,0xdf12,0xa013,0x4014,0xdf15,0x0b16,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_640x480x24[] = { 0x2c11,0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,0x4009,0x000c,0x000d,0xea10,0xdf12,0xf013,0x4014,0xdf15,0x0b16,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_800x600x8[] = { 0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,0x6009,0x000c,0x000d,0x7d10,0x5712,0x6413,0x4014,0x5715,0x9816,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_800x600x16[] = { 0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,0x6009,0x000c,0x000d,0x7d10,0x5712,0xc813,0x4014,0x5715,0x9816,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_800x600x24[] = { 0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,0x6009,0x000c,0x000d,0x7d10,0x5712,0x2c13,0x4014,0x5715,0x9816,0xc317,0xff18,0x001a,0x321b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1024x768x8[] = { 0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,0x6009,0x000c,0x000d,0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1024x768x16[] = { 0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,0x6009,0x000c,0x000d,0x0310,0xff12,0x0013,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x321b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1024x768x24[] = { 0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,0x6009,0x000c,0x000d,0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x321b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1280x1024x8[] = { 0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,0x6009,0x000c,0x000d,0x0310,0xff12,0xa013,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1280x1024x16[] = { 0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,0x6009,0x000c,0x000d,0x0310,0xff12,0x4013,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x321b,0x001d,0xffff };
static const uint16_t cirrus_crtc_1600x1200x8[] = { 0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,0x6009,0x000c,0x000d,0x0310,0xff12,0xc813,0x4014,0xff15,0x2416,0xc317,0xff18,0x001a,0x221b,0x001d,0xffff };

static const cirrus_mode_desc_t g_cirrus_modes[] = {
    {640, 480, 8,  cirrus_seq_640x480x8,  cirrus_graph_svgacolor, cirrus_crtc_640x480x8},
    {640, 480, 16, cirrus_seq_640x480x16, cirrus_graph_svgacolor, cirrus_crtc_640x480x16},
    {640, 480, 24, cirrus_seq_640x480x24, cirrus_graph_svgacolor, cirrus_crtc_640x480x24},
    {800, 600, 8,  cirrus_seq_800x600x8,  cirrus_graph_svgacolor, cirrus_crtc_800x600x8},
    {800, 600, 16, cirrus_seq_800x600x16, cirrus_graph_svgacolor, cirrus_crtc_800x600x16},
    {800, 600, 24, cirrus_seq_800x600x24, cirrus_graph_svgacolor, cirrus_crtc_800x600x24},
    {1024, 768, 8,  cirrus_seq_1024x768x8,  cirrus_graph_svgacolor, cirrus_crtc_1024x768x8},
    {1024, 768, 16, cirrus_seq_1024x768x16, cirrus_graph_svgacolor, cirrus_crtc_1024x768x16},
    {1024, 768, 24, cirrus_seq_1024x768x24, cirrus_graph_svgacolor, cirrus_crtc_1024x768x24},
    {1280, 1024, 8,  cirrus_seq_1280x1024x8,  cirrus_graph_svgacolor, cirrus_crtc_1280x1024x8},
    {1280, 1024, 16, cirrus_seq_1280x1024x16, cirrus_graph_svgacolor, cirrus_crtc_1280x1024x16},
    {1600, 1200, 8,  cirrus_seq_1600x1200x8,  cirrus_graph_svgacolor, cirrus_crtc_1600x1200x8},
};

const cirrus_mode_desc_t* cirrus_get_modes(size_t* count) {
    if (count) {
        *count = sizeof(g_cirrus_modes) / sizeof(g_cirrus_modes[0]);
    }
    return g_cirrus_modes;
}

uint32_t cirrus_mode_vram_required(const cirrus_mode_desc_t* mode) {
    if (!mode) return 0;
    uint64_t pixels = (uint64_t)mode->width * (uint64_t)mode->height;
    uint64_t bits = pixels * (uint64_t)mode->bpp;
    return (uint32_t)((bits + 7u) / 8u);
}

static int cirrus_mode_fits_vram(const cirrus_mode_desc_t* mode, uint32_t vram_bytes) {
    if (!mode) return 0;
    uint32_t required = cirrus_mode_vram_required(mode);
    if (!required) return 0;
    if (vram_bytes == 0) return 1;
    return (required <= vram_bytes);
}

const cirrus_mode_desc_t* cirrus_find_mode(uint16_t width,
                                           uint16_t height,
                                           uint8_t bpp,
                                           uint32_t vram_bytes) {
    size_t count = 0;
    const cirrus_mode_desc_t* modes = cirrus_get_modes(&count);
    for (size_t i = 0; i < count; ++i) {
        const cirrus_mode_desc_t* mode = &modes[i];
        if (width && mode->width != width) continue;
        if (height && mode->height != height) continue;
        if (bpp && mode->bpp != bpp) continue;
        if (!cirrus_mode_fits_vram(mode, vram_bytes)) continue;
        return mode;
    }
    return NULL;
}

const cirrus_mode_desc_t* cirrus_default_mode(uint32_t vram_bytes) {
    const cirrus_mode_desc_t* preferred = cirrus_find_mode(640, 480, 8, vram_bytes);
    if (preferred) {
        return preferred;
    }
    size_t count = 0;
    const cirrus_mode_desc_t* modes = cirrus_get_modes(&count);
    for (size_t i = 0; i < count; ++i) {
        if (cirrus_mode_fits_vram(&modes[i], vram_bytes)) {
            return &modes[i];
        }
    }
    return NULL;
}

static display_pixel_format_t cirrus_pixel_format(uint8_t bpp) {
    switch (bpp) {
        case 8: return DISPLAY_PIXEL_FORMAT_PAL_256;
        case 16: return DISPLAY_PIXEL_FORMAT_RGB_565;
        case 24: return DISPLAY_PIXEL_FORMAT_RGB_888;
        default: return DISPLAY_PIXEL_FORMAT_NONE;
    }
}

static uint32_t cirrus_detect_vram_bytes(void) {
    uint8_t sr0f = vga_seq_read(0x0F);
    uint8_t band = (uint8_t)((sr0f >> 3) & 0x03);
    uint32_t size;
    switch (band) {
        case 0x00: size = 256 * 1024u; break;
        case 0x01: size = 512 * 1024u; break;
        case 0x02: size = 1024 * 1024u; break;
        default:
            size = (sr0f & 0x80) ? (4u * 1024 * 1024) : (2u * 1024 * 1024);
            break;
    }
    return size;
}

typedef void (*cirrus_reg_writer_t)(uint8_t idx, uint8_t val);

static void cirrus_apply_reglist(const uint16_t* list, cirrus_reg_writer_t writer) {
    if (!list || !writer) return;
    while (*list != 0xFFFF) {
        uint16_t entry = *list++;
        uint8_t index = (uint8_t)(entry & 0xFFu);
        uint8_t value = (uint8_t)(entry >> 8);
        writer(index, value);
    }
}

static void cirrus_write_seq(uint8_t idx, uint8_t val) {
    vga_seq_write(idx, val);
}

static void cirrus_write_graph(uint8_t idx, uint8_t val) {
    vga_gc_write(idx, val);
}

static void cirrus_write_crtc(uint8_t idx, uint8_t val) {
    if (idx == 0x11) {
        val &= (uint8_t)~0x80;
    }
    vga_crtc_write(idx, val);
}

static void vga_program_standard_mode(uint8_t misc,
                                      const uint8_t* seq,
                                      const uint8_t* crtc,
                                      const uint8_t* graph,
                                      const uint8_t* attr) {
    vga_misc_write(misc);
    if (seq) {
        for (int i = 0; i < 5; i++) {
            vga_seq_write((uint8_t)i, seq[i]);
        }
    }
    uint8_t crt11 = vga_crtc_read(0x11);
    vga_crtc_write(0x11, (uint8_t)(crt11 & ~0x80));
    if (crtc) {
        for (int i = 0; i < 25; i++) {
            vga_crtc_write((uint8_t)i, crtc[i]);
        }
    }
    if (graph) {
        for (int i = 0; i < 9; i++) {
            vga_gc_write((uint8_t)i, graph[i]);
        }
    }
    if (attr) {
        for (int i = 0; i < 21; i++) {
            vga_attr_write((uint8_t)i, attr[i]);
        }
    }
    vga_attr_reenable_video();
}

int cirrus_gpu_detect(const pci_device_t* dev, gpu_info_t* out) {
    if (!dev || !out) return 0;
    if (dev->vendor_id != CIRRUS_VENDOR_ID) return 0;
    if (dev->device_id != CIRRUS_DEVICE_ID_GD5446) return 0;

    out->type = GPU_TYPE_CIRRUS;
    copy_name(out->name, sizeof(out->name), "Cirrus Logic GD5446");
    out->pci = *dev;
    out->capabilities = GPU_CAP_LINEAR_FB | GPU_CAP_ACCEL_2D | GPU_CAP_HW_CURSOR;

    uint32_t fb_bar = dev->bars[0].raw;
    uint32_t mmio_bar = dev->bars[1].raw;
    out->framebuffer_bar = (fb_bar && ((fb_bar & 0x1) == 0)) ? 0 : 0xFF;
    out->framebuffer_base = 0;
    out->framebuffer_size = cirrus_detect_vram_bytes();
    if ((fb_bar & 0x1) == 0 && fb_bar != 0) {
        out->framebuffer_bar = 0;
        out->framebuffer_base = (uint32_t)dev->bars[0].base;
        uint64_t bar_sz = dev->bars[0].size;
        if (bar_sz != 0 && bar_sz < out->framebuffer_size) {
            out->framebuffer_size = (uint32_t)bar_sz;
        }
    }
    out->mmio_bar = (mmio_bar && ((mmio_bar & 0x1) == 0)) ? (uint32_t)dev->bars[1].base : 0;
    out->framebuffer_width = 0;
    out->framebuffer_height = 0;
    out->framebuffer_pitch = 0;
    out->framebuffer_bpp = 0;
    out->framebuffer_ptr = NULL;

    return 1;
}

int cirrus_set_mode_desc(const pci_device_t* dev,
                         const cirrus_mode_desc_t* mode,
                         display_mode_info_t* out_mode,
                         gpu_info_t* gpu) {
    if (!dev || !mode || !out_mode || !gpu) return 0;
    if (gpu->framebuffer_base == 0 || gpu->framebuffer_bar == 0xFF) return 0;

    display_pixel_format_t pixel_format = cirrus_pixel_format(mode->bpp);
    if (pixel_format == DISPLAY_PIXEL_FORMAT_NONE) {
        return 0;
    }

    uint32_t irq_flags = interrupts_save_disable();
    int result = 0;

    do {
        vga_program_standard_mode(0xE3, std_seq_640x480, std_crtc_640x480, std_graph_640x480, std_attr_640x480);

        uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->function, 0x04);
        cmd |= 0x0002; // Memory Space
        cmd |= 0x0004; // Bus Master
        pci_config_write16(dev->bus, dev->device, dev->function, 0x04, cmd);

        vga_misc_write(0xE3);
        vga_seq_write(0x06, 0x12);
        cirrus_apply_reglist(mode->seq, cirrus_write_seq);
        if (mode->graph) {
            cirrus_apply_reglist(mode->graph, cirrus_write_graph);
        }
        cirrus_apply_reglist(mode->crtc, cirrus_write_crtc);

        vga_pel_mask_write(0x00);
        vga_pel_mask_read(); vga_pel_mask_read(); vga_pel_mask_read(); vga_pel_mask_read();
        vga_pel_mask_write(0x00);
        vga_pel_mask_write(0xFF);

        vga_attr_mask(0x10, 0x01, 0x01);
        vga_attr_index_write(0x20);

        uint32_t pitch = ((uint32_t)mode->width * (uint32_t)mode->bpp + 7u) / 8u;
        uint8_t crt1D = vga_crtc_read(0x1D);
        crt1D |= 0x08; // Linear Framebuffer aktivieren
        vga_crtc_write(0x1D, crt1D);

        uint32_t base = gpu->framebuffer_base;
        vga_crtc_write(0x1A, (uint8_t)((base >> 24) & 0x0F));
        vga_crtc_write(0x1C, (uint8_t)((base >> 16) & 0xFF));

        out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
        out_mode->pixel_format = pixel_format;
        out_mode->width = mode->width;
        out_mode->height = mode->height;
        out_mode->bpp = mode->bpp;
        out_mode->pitch = pitch;
        out_mode->phys_base = base;
        out_mode->framebuffer = (volatile uint8_t*)(uintptr_t)base;

        gpu->framebuffer_width = mode->width;
        gpu->framebuffer_height = mode->height;
        gpu->framebuffer_pitch = pitch;
        gpu->framebuffer_bpp = mode->bpp;
        gpu->framebuffer_ptr = out_mode->framebuffer;

        result = 1;
    } while (0);

    interrupts_restore(irq_flags);
    return result;
}

int cirrus_set_mode_640x480x8(const pci_device_t* dev, display_mode_info_t* out_mode, gpu_info_t* gpu) {
    const cirrus_mode_desc_t* mode = cirrus_find_mode(640, 480, 8, gpu ? gpu->framebuffer_size : 0);
    if (!mode) {
        return 0;
    }
    return cirrus_set_mode_desc(dev, mode, out_mode, gpu);
}

int cirrus_restore_text_mode(const pci_device_t* dev) {
    (void)dev;
    vga_program_standard_mode(0x67, std_seq_text, std_crtc_text, std_graph_text, std_attr_text);

    vga_seq_write(0x06, 0x12);
    cirrus_apply_reglist(cirrus_seq_text, cirrus_write_seq);
    cirrus_apply_reglist(cirrus_graph_text, cirrus_write_graph);
    cirrus_apply_reglist(cirrus_crtc_text, cirrus_write_crtc);

    uint8_t crt1D = vga_crtc_read(0x1D);
    crt1D &= (uint8_t)~0x08;
    vga_crtc_write(0x1D, crt1D);

    vga_attr_mask(0x10, 0x01, 0x00);
    vga_attr_index_write(0x20);
    vga_dac_reset_text_palette();
    vga_load_font_8x16();
    return 1;
}

void cirrus_dump_state(const pci_device_t* dev) {
    (void)dev;

    static const uint8_t seq_main[]   = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F };
    static const uint8_t seq_ext[]    = { 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F };
    static const uint8_t crtc_main[]  = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
                                          0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18 };
    static const uint8_t crtc_ext[]   = { 0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x22,0x24,0x27,0x2D,0x2E };
    static const uint8_t gc_main[]    = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09 };
    static const uint8_t gc_ext[]     = { 0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F };
    static const uint8_t attr_idx[]   = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x10,0x12,0x13,0x14 };

    dump_block("Seq regs:", vga_seq_read, seq_main, sizeof(seq_main));
    dump_block("Seq ext:",  vga_seq_read, seq_ext,  sizeof(seq_ext));

    dump_block("CRTC regs:", vga_crtc_read, crtc_main, sizeof(crtc_main));
    dump_block("CRTC ext:",  vga_crtc_read, crtc_ext,  sizeof(crtc_ext));

    dump_block("Graphics regs:", vga_gc_read, gc_main, sizeof(gc_main));
    dump_block("Graphics ext:",  vga_gc_read, gc_ext,  sizeof(gc_ext));

    console_write("      Attr regs:");
    for (size_t i = 0; i < sizeof(attr_idx); i++) {
        console_write(" AR");
        print_byte_hex(attr_idx[i]);
        console_write("=");
        print_byte_hex(vga_attr_read(attr_idx[i]));
        if ((i & 0x03) == 0x03) console_writeln("");
        else console_write("  ");
    }
    console_writeln("");

    console_write("      Misc=");
    print_byte_hex(vga_misc_read());
    console_writeln("");
}
