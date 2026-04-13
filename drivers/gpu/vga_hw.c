#include "vga_hw.h"
#include "../../config.h"
#include <stddef.h>

#if CONFIG_ARCH_X86
#include "../../arch/x86/io.h"
#include "../../fonts/font8x16.h"
static const uint8_t VGA_DEFAULT_PALETTE[256][3] = {
#include "vga_palette_generated.inc"
};
static inline void vga_io_delay(void) {
    outb(0x80, 0);
}
#endif

static inline void vga_attr_reset(void) {
#if CONFIG_ARCH_X86
    (void)inb(0x3DA);
#endif
}

#if CONFIG_ARCH_X86
static inline void vga_attr_write_sequence(uint8_t index, uint8_t value) {
    vga_attr_reset();
    outb(0x3C0, index);
    outb(0x3C0, value);
}

static inline void vga_attr_video_enable_sequence(void) {
    vga_attr_reset();
    outb(0x3C0, 0x20);
}
#endif

uint8_t vga_seq_read(uint8_t index) {
#if CONFIG_ARCH_X86
    outb(0x3C4, index);
    return inb(0x3C5);
#else
    (void)index;
    return 0;
#endif
}

void vga_seq_write(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    outb(0x3C4, index);
    outb(0x3C5, value);
#else
    (void)index; (void)value;
#endif
}

uint8_t vga_crtc_read(uint8_t index) {
#if CONFIG_ARCH_X86
    outb(0x3D4, index);
    return inb(0x3D5);
#else
    (void)index;
    return 0;
#endif
}

void vga_crtc_write(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    outb(0x3D4, index);
    outb(0x3D5, value);
#else
    (void)index; (void)value;
#endif
}

uint8_t vga_gc_read(uint8_t index) {
#if CONFIG_ARCH_X86
    outb(0x3CE, index);
    return inb(0x3CF);
#else
    (void)index;
    return 0;
#endif
}

void vga_gc_write(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    outb(0x3CE, index);
    outb(0x3CF, value);
#else
    (void)index; (void)value;
#endif
}

uint8_t vga_attr_read(uint8_t index) {
#if CONFIG_ARCH_X86
    vga_attr_reset();
    outb(0x3C0, index);
    uint8_t value = inb(0x3C1);
    vga_attr_video_enable_sequence();
    return value;
#else
    (void)index;
    return 0;
#endif
}

uint8_t vga_misc_read(void) {
#if CONFIG_ARCH_X86
    return inb(0x3CC);
#else
    return 0;
#endif
}

void vga_attr_write(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    vga_attr_write_sequence(index, value);
#else
    (void)index; (void)value;
#endif
}

void vga_attr_write_and_reenable(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    vga_attr_write_sequence(index, value);
    vga_attr_video_enable_sequence();
#else
    (void)index; (void)value;
#endif
}

void vga_attr_reenable_video(void) {
#if CONFIG_ARCH_X86
    vga_attr_video_enable_sequence();
#endif
}

void vga_misc_write(uint8_t value) {
#if CONFIG_ARCH_X86
    outb(0x3C2, value);
#else
    (void)value;
#endif
}

void vga_dac_set_entry(uint8_t index, uint8_t r6, uint8_t g6, uint8_t b6) {
#if CONFIG_ARCH_X86
    outb(0x3C8, index);
    vga_io_delay();
    outb(0x3C9, (uint8_t)(r6 & 0x3F));
    vga_io_delay();
    outb(0x3C9, (uint8_t)(g6 & 0x3F));
    vga_io_delay();
    outb(0x3C9, (uint8_t)(b6 & 0x3F));
    vga_io_delay();
#else
    (void)index; (void)r6; (void)g6; (void)b6;
#endif
}

void vga_dac_set_entry_rgb(uint8_t index, uint8_t r8, uint8_t g8, uint8_t b8) {
    vga_dac_set_entry(index, (uint8_t)(r8 >> 2), (uint8_t)(g8 >> 2), (uint8_t)(b8 >> 2));
}

void vga_dac_reset_text_palette(void) {
    static const uint8_t text_palette[16][3] = {
        {0x00,0x00,0x00}, {0x00,0x00,0x2A}, {0x00,0x2A,0x00}, {0x00,0x2A,0x2A},
        {0x2A,0x00,0x00}, {0x2A,0x00,0x2A}, {0x15,0x15,0x00}, {0x2A,0x2A,0x2A},
        {0x15,0x15,0x15}, {0x15,0x15,0x3F}, {0x15,0x3F,0x15}, {0x15,0x3F,0x3F},
        {0x3F,0x15,0x15}, {0x3F,0x15,0x3F}, {0x3F,0x3F,0x15}, {0x3F,0x3F,0x3F}
    };
#if CONFIG_ARCH_X86
    for (size_t i = 0; i < 16; i++) {
        vga_dac_set_entry((uint8_t)i, text_palette[i][0], text_palette[i][1], text_palette[i][2]);
    }
#else
    (void)text_palette;
#endif
}

void vga_dac_load_default_palette(void) {
#if CONFIG_ARCH_X86
    for (uint16_t i = 0; i < 256; i++) {
        vga_dac_set_entry((uint8_t)i,
                          VGA_DEFAULT_PALETTE[i][0],
                          VGA_DEFAULT_PALETTE[i][1],
                          VGA_DEFAULT_PALETTE[i][2]);
    }

    static const uint8_t kDarkRainbow[16][3] = {
        {0x20,0x04,0x04}, {0x20,0x10,0x04}, {0x20,0x18,0x04}, {0x18,0x20,0x04},
        {0x08,0x20,0x04}, {0x04,0x20,0x10}, {0x04,0x20,0x18}, {0x04,0x18,0x20},
        {0x04,0x08,0x20}, {0x10,0x04,0x20}, {0x18,0x04,0x20}, {0x20,0x04,0x18},
        {0x20,0x04,0x10}, {0x20,0x0C,0x04}, {0x20,0x14,0x04}, {0x18,0x20,0x04}
    };
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t idx = (uint8_t)(240 + i);
        vga_dac_set_entry(idx, kDarkRainbow[i][0], kDarkRainbow[i][1], kDarkRainbow[i][2]);
    }
#endif
}

uint8_t vga_pel_mask_read(void) {
#if CONFIG_ARCH_X86
    return inb(0x3C6);
#else
    return 0;
#endif
}

void vga_pel_mask_write(uint8_t value) {
#if CONFIG_ARCH_X86
    outb(0x3C6, value);
#else
    (void)value;
#endif
}

void vga_attr_mask(uint8_t index, uint8_t mask, uint8_t value) {
#if CONFIG_ARCH_X86
    uint8_t current = vga_attr_read(index);
    current = (uint8_t)((current & ~mask) | (value & mask));
    vga_attr_write_and_reenable(index, current);
#else
    (void)index; (void)mask; (void)value;
#endif
}

void vga_attr_index_write(uint8_t value) {
#if CONFIG_ARCH_X86
    vga_attr_reset();
    outb(0x3C0, value);
#else
    (void)value;
#endif
}

void vga_set_mode_640x400x256(void) {
#if CONFIG_ARCH_X86
    vga_misc_write(0x63);
    vga_seq_write(0x00, 0x01);
    vga_seq_write(0x01, 0x01);
    vga_seq_write(0x02, 0x0F);
    vga_seq_write(0x03, 0x00);
    vga_seq_write(0x04, 0x0E);
    vga_seq_write(0x00, 0x03);

    uint8_t crt11 = vga_crtc_read(0x11);
    vga_crtc_write(0x11, (uint8_t)(crt11 & ~0x80));

    static const uint8_t crtc640x400[25] = {
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x50,0x40,0x96,0xB9,0xA3,
        0xFF
    };
    for (uint8_t i = 0; i < 25; i++) vga_crtc_write(i, crtc640x400[i]);
    vga_crtc_write(0x11, (uint8_t)(crt11 | 0x80));

    vga_gc_write(0x00, 0x00);
    vga_gc_write(0x01, 0x00);
    vga_gc_write(0x02, 0x00);
    vga_gc_write(0x03, 0x00);
    vga_gc_write(0x04, 0x00);
    vga_gc_write(0x05, 0x40);
    vga_gc_write(0x06, 0x05);
    vga_gc_write(0x07, 0x0F);
    vga_gc_write(0x08, 0xFF);

    for (uint8_t i = 0; i < 16; i++) vga_attr_write(i, i);
    vga_attr_write(0x10, 0x41);
    vga_attr_write(0x11, 0x00);
    vga_attr_write(0x12, 0x0F);
    vga_attr_write(0x13, 0x00);
    vga_attr_write(0x14, 0x00);
    vga_attr_reenable_video();
    vga_dac_load_default_palette();
#endif
}

void vga_set_mode13(void) {
#if CONFIG_ARCH_X86
    vga_misc_write(0x63);
    vga_seq_write(0x00, 0x01);
    vga_seq_write(0x01, 0x01);
    vga_seq_write(0x02, 0x0F);
    vga_seq_write(0x03, 0x00);
    vga_seq_write(0x04, 0x0E);
    vga_seq_write(0x00, 0x03);

    uint8_t crt11 = vga_crtc_read(0x11);
    vga_crtc_write(0x11, (uint8_t)(crt11 & ~0x80));

    static const uint8_t crtc13[25] = {
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,
        0xFF
    };
    for (uint8_t i = 0; i < 25; i++) vga_crtc_write(i, crtc13[i]);
    vga_crtc_write(0x11, (uint8_t)(crt11 | 0x80));

    vga_gc_write(0x00, 0x00);
    vga_gc_write(0x01, 0x00);
    vga_gc_write(0x02, 0x00);
    vga_gc_write(0x03, 0x00);
    vga_gc_write(0x04, 0x00);
    vga_gc_write(0x05, 0x40);
    vga_gc_write(0x06, 0x05);
    vga_gc_write(0x07, 0x0F);
    vga_gc_write(0x08, 0xFF);

    for (uint8_t i = 0; i < 16; i++) vga_attr_write(i, i);
    vga_attr_write(0x10, 0x41);
    vga_attr_write(0x11, 0x00);
    vga_attr_write(0x12, 0x0F);
    vga_attr_write(0x13, 0x00);
    vga_attr_write(0x14, 0x00);
    vga_attr_reenable_video();
    vga_dac_load_default_palette();
#endif
}

void vga_set_mode3(void) {
#if CONFIG_ARCH_X86
    vga_misc_write(0x67);
    
    // Sequencer
    vga_seq_write(0x00, 0x01); // Reset
    vga_seq_write(0x01, 0x00); // Clock mode
    vga_seq_write(0x02, 0x03); // Map mask
    vga_seq_write(0x03, 0x00); // Char map select
    vga_seq_write(0x04, 0x02); // Memory mode
    vga_seq_write(0x00, 0x03); // End reset

    // Unlock CRTC 0-7
    uint8_t cr11 = vga_crtc_read(0x11);
    vga_crtc_write(0x11, cr11 & 0x7F);

    static const uint8_t crtc3[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    for (uint8_t i = 0; i < 25; i++) vga_crtc_write(i, crtc3[i]);

    // Graphics Controller
    vga_gc_write(0x00, 0x00);
    vga_gc_write(0x01, 0x00);
    vga_gc_write(0x02, 0x00);
    vga_gc_write(0x03, 0x00);
    vga_gc_write(0x04, 0x00);
    vga_gc_write(0x05, 0x10); // Odd/Even
    vga_gc_write(0x06, 0x0E); // Map to 0xB8000
    vga_gc_write(0x07, 0x00);
    vga_gc_write(0x08, 0xFF);

    // Attribute Controller
    for (uint8_t i = 0; i < 16; i++) vga_attr_write(i, i);
    vga_attr_write(0x10, 0x04); // Text mode
    vga_attr_write(0x11, 0x00);
    vga_attr_write(0x12, 0x0F);
    vga_attr_write(0x13, 0x00);
    vga_attr_write(0x14, 0x00);
    vga_attr_reenable_video();

    vga_dac_reset_text_palette();
    vga_load_font_8x16();
#endif
}

void vga_load_font_8x16(void) {
#if CONFIG_ARCH_X86
    vga_seq_write(0x00, 0x01);
    vga_seq_write(0x02, 0x04);
    vga_seq_write(0x04, 0x07);
    vga_seq_write(0x00, 0x03);
    vga_gc_write(0x04, 0x02);
    vga_gc_write(0x05, 0x00);
    vga_gc_write(0x06, 0x04);

    volatile uint8_t* dest = (volatile uint8_t*)0xA0000u;
    for (uint16_t ch = 0; ch < 256; ch++) {
        const uint8_t* src = font8x16_get((uint8_t)ch);
        volatile uint8_t* d = dest + ch * 32u;
        for (int i = 0; i < 16; i++) d[i] = src[i];
        for (int i = 16; i < 32; i++) d[i] = 0;
    }

    vga_seq_write(0x00, 0x01);
    vga_seq_write(0x02, 0x03);
    vga_seq_write(0x04, 0x03);
    vga_seq_write(0x00, 0x03);
    uint8_t val = (vga_misc_read() & 0x01) ? 0x0E : 0x0A;
    vga_gc_write(0x06, val);
    vga_gc_write(0x04, 0x00);
    vga_gc_write(0x05, 0x10);
#endif
}
