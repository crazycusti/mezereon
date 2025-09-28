#include "cirrus.h"
#include "../pci.h"
#include "vga_hw.h"
#include "../../console.h"
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

static uint32_t bar_base(uint32_t raw) {
    if (raw & 0x1) {
        return raw & ~0x3u; // I/O base
    }
    return raw & ~0xFu; // memory base
}

static uint32_t bar_size(const pci_device_t* dev, uint8_t index) {
    if (index >= 6) return 0;
    uint32_t original = pci_config_read32(dev->bus, dev->device, dev->function, (uint8_t)(0x10 + index * 4));
    if (original == 0x0 || original == 0xFFFFFFFFu) {
        return 0;
    }

    pci_config_write32(dev->bus, dev->device, dev->function, (uint8_t)(0x10 + index * 4), 0xFFFFFFFFu);
    uint32_t mask = pci_config_read32(dev->bus, dev->device, dev->function, (uint8_t)(0x10 + index * 4));
    pci_config_write32(dev->bus, dev->device, dev->function, (uint8_t)(0x10 + index * 4), original);

    if (original & 0x1) {
        mask &= ~0x3u;
    } else {
        mask &= ~0xFu;
    }
    if (mask == 0 || mask == 0xFFFFFFFFu) {
        return 0;
    }
    uint32_t size = (~mask) + 1;
    return size;
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

    uint32_t fb_bar = dev->bars[0];
    uint32_t mmio_bar = dev->bars[1];
    out->framebuffer_bar = (fb_bar && ((fb_bar & 0x1) == 0)) ? 0 : 0xFF;
    out->framebuffer_base = 0;
    out->framebuffer_size = 0;
    if ((fb_bar & 0x1) == 0 && fb_bar != 0) {
        out->framebuffer_bar = 0;
        out->framebuffer_base = bar_base(fb_bar);
        out->framebuffer_size = bar_size(dev, 0);
    }
    out->mmio_bar = (mmio_bar && ((mmio_bar & 0x1) == 0)) ? bar_base(mmio_bar) : 0;
    out->framebuffer_width = 0;
    out->framebuffer_height = 0;
    out->framebuffer_pitch = 0;
    out->framebuffer_bpp = 0;
    out->framebuffer_ptr = NULL;

    return 1;
}

int cirrus_set_mode_640x480x8(const pci_device_t* dev, display_mode_info_t* out_mode, gpu_info_t* gpu) {
    if (!dev || !out_mode || !gpu) return 0;
    if (gpu->framebuffer_base == 0 || gpu->framebuffer_bar == 0xFF) return 0;

    vga_program_standard_mode(0xE3, std_seq_640x480, std_crtc_640x480, std_graph_640x480, std_attr_640x480);

    uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x0002; // Memory Space
    cmd |= 0x0004; // Bus Master
    pci_config_write16(dev->bus, dev->device, dev->function, 0x04, cmd);

    vga_misc_write(0xE3);
    vga_seq_write(0x06, 0x12);
    cirrus_apply_reglist(cirrus_seq_640x480x8, cirrus_write_seq);
    cirrus_apply_reglist(cirrus_graph_svgacolor, cirrus_write_graph);
    cirrus_apply_reglist(cirrus_crtc_640x480x8, cirrus_write_crtc);

    vga_pel_mask_write(0x00);
    vga_pel_mask_read(); vga_pel_mask_read(); vga_pel_mask_read(); vga_pel_mask_read();
    vga_pel_mask_write(0x00);
    vga_pel_mask_write(0xFF);

    vga_attr_mask(0x10, 0x01, 0x01);
    vga_attr_index_write(0x20);

    const uint16_t width = 640;
    const uint16_t height = 480;
    const uint8_t bpp = 8;
    const uint32_t pitch = 640u;

    uint8_t crt1D = vga_crtc_read(0x1D);
    crt1D |= 0x08; // Linear Framebuffer aktivieren
    vga_crtc_write(0x1D, crt1D);

    uint32_t base = gpu->framebuffer_base;
    vga_crtc_write(0x1A, (uint8_t)((base >> 24) & 0x0F));
    vga_crtc_write(0x1C, (uint8_t)((base >> 16) & 0xFF));

    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    out_mode->width = width;
    out_mode->height = height;
    out_mode->bpp = bpp;
    out_mode->pitch = pitch;
    out_mode->phys_base = base;
    out_mode->framebuffer = (volatile uint8_t*)(uintptr_t)base;

    gpu->framebuffer_width = width;
    gpu->framebuffer_height = height;
    gpu->framebuffer_pitch = pitch;
    gpu->framebuffer_bpp = bpp;
    gpu->framebuffer_ptr = out_mode->framebuffer;

    return 1;
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
