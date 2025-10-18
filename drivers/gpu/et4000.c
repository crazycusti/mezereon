#include "et4000.h"
#include "vga_hw.h"
#include "fb_accel.h"
#include "et4000_common.h"
#include "../../config.h"
#include "../../console.h"
#if CONFIG_ARCH_X86
#include "../../arch/x86/io.h"
#include "../../interrupts.h"
#endif

#include <stdint.h>

int g_is_ax_variant = 0;

#if !CONFIG_ARCH_X86

int et4000_detect(gpu_info_t* out_info) {
    (void)out_info;
    return 0;
}

int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode) {
    (void)gpu; (void)out_mode; (void)mode;
    return 0;
}

int et4k_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode,
                  uint16_t width, uint16_t height, uint8_t bpp) {
    (void)gpu; (void)out_mode; (void)width; (void)height; (void)bpp;
    return 0;
}

et4000_mode_t et4k_choose_default_mode(int is_ax_variant, uint32_t vram_bytes) {
    (void)is_ax_variant; (void)vram_bytes;
    return ET4000_MODE_640x480x4;
}

void et4000_restore_text_mode(void) {}
void et4000_dump_bank(uint8_t bank, uint32_t offset, uint32_t length) {
    (void)bank; (void)offset; (void)length;
}

int et4000_capture_dump(uint8_t bank, uint32_t offset, uint32_t length) {
    (void)bank; (void)offset; (void)length;
    return 0;
}

void et4000_debug_dump(void) {
    console_writeln("et4000-debug: not supported on this architecture.");
}

void et4000_set_debug_trace(int enabled) { (void)enabled; }
int et4k_debug_trace_enabled(void) { return 0; }
void et4k_disable_ax_engine(const char* reason) { (void)reason; }
int et4k_detection_toggle_ok(void) { return 0; }
int et4k_detection_latch_ok(void) { return 0; }
int et4k_detection_signature_ok(void) { return 0; }
int et4k_detection_alias_limited(void) { return 0; }
uint32_t et4k_detection_alias_limit_bytes(void) { return 0; }

#else

#define ET4K_EXT_PORT        0x3BF
#define ET4K_SIGNATURE_PORT  0x3CD
#define ET4K_PORT_BANK       0x3CB
#define ET4K_PORT_SEGMENT    ET4K_SIGNATURE_PORT
#define VGA_WINDOW_PHYS      0xA0000u
#define VGA_WINDOW_SIZE      0x40000u

typedef struct {
    uint8_t* buffer;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
    int      dirty;
} et4k_fb_state_t;

static uint8_t g_et4k_shadow[640u * 480u];
static volatile uint8_t* g_et4k_vram_window = NULL;
static et4k_fb_state_t g_et4k_fb = { NULL, 0, 0, 0, 0, 0 };

static struct {
    int detected;
    int signature_ok;
} g_et4k_detect = { 0, 0 };

static int g_et4k_debug_trace = 1;

#define ET4K_PORT_SEQ_INDEX   0x3C4
#define ET4K_PORT_SEQ_DATA    0x3C5
#define ET4K_PORT_CRTC_INDEX  0x3D4
#define ET4K_PORT_CRTC_DATA   0x3D5
#define ET4K_PORT_GC_INDEX    0x3CE
#define ET4K_PORT_GC_DATA     0x3CF
#define ET4K_PORT_ATTR        0x3C0
#define ET4K_PORT_STATUS      0x3DA

#define ET4K_STATUS_VRETRACE  0x08u
#define ET4K_STATUS_HRETRACE  0x01u
#define ET4K_SYNC_TIMEOUT_ITERATIONS 200000u

static inline int et4k_trace_enabled(void) {
    return g_et4k_debug_trace;
}

static inline uint32_t et4k_irq_guard_acquire(void) {
    return interrupts_save_disable();
}

static inline void et4k_irq_guard_release(uint32_t flags) {
    interrupts_restore(flags);
}

static void et4k_log(const char* msg) {
    if (!msg || !et4k_trace_enabled()) return;
    console_write("[et4k] ");
    console_writeln(msg);
}

static void et4k_log_hex(const char* label, uint32_t value) {
    if (!label || !et4k_trace_enabled()) return;
    console_write("[et4k] ");
    console_write(label);
    console_write("=0x");
    console_write_hex32(value);
    console_write("\n");
}

static void et4k_log_dec(const char* label, uint32_t value) {
    if (!label || !et4k_trace_enabled()) return;
    console_write("[et4k] ");
    console_write(label);
    console_write("=");
    console_write_dec(value);
    console_write("\n");
}

#define ET4K_COM1_BASE 0x3F8u
#define ET4K_UART_LSR_OFFSET 5u
#define ET4K_UART_LSR_THRE 0x20u

static inline void et4k_serial_heartbeat(uint8_t marker) {
    uint8_t status = inb(ET4K_COM1_BASE + ET4K_UART_LSR_OFFSET);
    if ((status & ET4K_UART_LSR_THRE) == 0u) {
        return;
    }
    outb(ET4K_COM1_BASE, marker);
}

static void et4k_log_reg_write(const char* block, uint8_t index, uint8_t value) {
    if (!block || !et4k_trace_enabled()) return;
    console_write("[et4k] ");
    console_write(block);
    console_write("[");
    console_write_dec(index);
    console_write("]=0x");
    console_write_hex32(value);
    console_write("\n");
}

static inline void et4k_io_wait(void) {
    outb(0x80, 0);
}

static inline void et4k_seq_write(uint8_t index, uint8_t value) {
    outb(ET4K_PORT_SEQ_INDEX, index);
    et4k_io_wait();
    outb(ET4K_PORT_SEQ_DATA, value);
    et4k_io_wait();
}

static inline uint8_t et4k_seq_read(uint8_t index) {
    outb(ET4K_PORT_SEQ_INDEX, index);
    et4k_io_wait();
    uint8_t value = inb(ET4K_PORT_SEQ_DATA);
    et4k_io_wait();
    return value;
}

static inline void et4k_crtc_write(uint8_t index, uint8_t value) {
    outb(ET4K_PORT_CRTC_INDEX, index);
    et4k_io_wait();
    outb(ET4K_PORT_CRTC_DATA, value);
    et4k_io_wait();
}

static inline uint8_t et4k_crtc_read(uint8_t index) {
    outb(ET4K_PORT_CRTC_INDEX, index);
    et4k_io_wait();
    uint8_t value = inb(ET4K_PORT_CRTC_DATA);
    et4k_io_wait();
    return value;
}

static inline void et4k_gc_write(uint8_t index, uint8_t value) {
    outb(ET4K_PORT_GC_INDEX, index);
    et4k_io_wait();
    outb(ET4K_PORT_GC_DATA, value);
    et4k_io_wait();
}

static inline uint8_t et4k_gc_read(uint8_t index) {
    outb(ET4K_PORT_GC_INDEX, index);
    et4k_io_wait();
    uint8_t value = inb(ET4K_PORT_GC_DATA);
    et4k_io_wait();
    return value;
}

static inline uint8_t et4k_status_read(void) {
    uint8_t value = inb(ET4K_PORT_STATUS);
    et4k_io_wait();
    return value;
}

static void et4k_reset_window_registers(void) {
    uint32_t irq_flags = et4k_irq_guard_acquire();

    uint8_t ext_before = inb(ET4K_EXT_PORT);
    et4k_io_wait();
    uint8_t seg_before = inb(ET4K_PORT_SEGMENT);
    et4k_io_wait();
    uint8_t bank_before = inb(ET4K_PORT_BANK);
    et4k_io_wait();

    uint8_t ext_unlock = (uint8_t)(ext_before | 0x03u);
    if (ext_unlock != ext_before) {
        outb(ET4K_EXT_PORT, ext_unlock);
        et4k_io_wait();
    }

    outb(ET4K_PORT_SEGMENT, 0x00);
    et4k_io_wait();
    outb(ET4K_PORT_BANK, 0x00);
    et4k_io_wait();

    if (et4k_trace_enabled()) {
        uint8_t seg_after = inb(ET4K_PORT_SEGMENT);
        et4k_io_wait();
        uint8_t bank_after = inb(ET4K_PORT_BANK);
        et4k_io_wait();
        et4k_log_hex("window.segment.before", seg_before);
        et4k_log_hex("window.segment.after", seg_after);
        et4k_log_hex("window.bank.before", bank_before);
        et4k_log_hex("window.bank.after", bank_after);
    }

    if (ext_before != ext_unlock) {
        outb(ET4K_EXT_PORT, ext_before);
        et4k_io_wait();
    }

    et4k_irq_guard_release(irq_flags);
}

static int et4k_wait_status(uint8_t mask, uint8_t expected_bits) {
    for (uint32_t i = 0; i < ET4K_SYNC_TIMEOUT_ITERATIONS; ++i) {
        uint8_t status = et4k_status_read();
        if ((status & mask) == expected_bits) {
            return 1;
        }
    }
    return 0;
}

static void et4k_wait_vblank_window(void) {
    if (!et4k_wait_status(ET4K_STATUS_VRETRACE, 0)) {
        et4k_log("wait_vblank: timeout waiting for display active (skipping sync)");
        return;
    }
    if (!et4k_wait_status(ET4K_STATUS_VRETRACE, ET4K_STATUS_VRETRACE)) {
        et4k_log("wait_vblank: timeout waiting for retrace (skipping sync)");
    }
}

static inline void et4k_misc_write(uint8_t value) {
    outb(0x3C2, value);
    et4k_io_wait();
}

static inline void et4k_attr_write(uint8_t index, uint8_t value) {
    (void)et4k_status_read();
    outb(ET4K_PORT_ATTR, index);
    et4k_io_wait();
    outb(ET4K_PORT_ATTR, value);
    et4k_io_wait();
}

static void et4k_memset8(void* dst, uint8_t value, uint32_t count) {
    uint8_t* ptr = (uint8_t*)dst;
    for (uint32_t i = 0; i < count; ++i) {
        ptr[i] = value;
    }
}

static void et4k_bzero(void* dst, uint32_t count) {
    et4k_memset8(dst, 0u, count);
}

static void et4k_copy_string(char* dst, uint32_t dst_len, const char* src) {
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

static const uint8_t k_seq_mode12[5] = { 0x03, 0x01, 0x0F, 0x00, 0x02 };
static const uint8_t k_crtc_mode12[25] = {
    0x5F,0x4F,0x50,0x82,0x54,0x80,0x0B,0x3E,
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
    0xEA,0x0C,0xDF,0x50,0x00,0xE7,0x04,0xE3,
    0xFF
};
static const uint8_t k_graph_mode12[9] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF };
static const uint8_t k_attr_mode12[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x01,0x00,0x0F,0x00,0x00
};

static const uint8_t k_seq_text[5] = { 0x03,0x00,0x03,0x00,0x02 };
static const uint8_t k_crtc_text[25] = {
    0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,
    0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x50,
    0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,
    0xFF
};
static const uint8_t k_graph_text[9] = { 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF };
static const uint8_t k_attr_text[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x0C,0x00,0x0F,0x08,0x00
};

static const uint8_t k_palette16[16][3] = {
    {0x00,0x00,0x00}, {0x00,0x00,0x2A}, {0x00,0x2A,0x00}, {0x00,0x2A,0x2A},
    {0x2A,0x00,0x00}, {0x2A,0x00,0x2A}, {0x15,0x15,0x00}, {0x2A,0x2A,0x2A},
    {0x15,0x15,0x15}, {0x15,0x15,0x3F}, {0x15,0x3F,0x15}, {0x15,0x3F,0x3F},
    {0x3F,0x15,0x15}, {0x3F,0x15,0x3F}, {0x3F,0x3F,0x15}, {0x3F,0x3F,0x3F}
};

static int et4k_fb_fill_rect(void* ctx, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height, uint8_t color);
static void et4k_fb_sync(void* ctx);
static void et4k_fb_mark_dirty(void* ctx, uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height);

static const fb_accel_ops_t g_et4k_fb_ops = {
    et4k_fb_fill_rect,
    et4k_fb_sync,
    et4k_fb_mark_dirty
};

static int et4k_map_vram_window(uint32_t phys_base) {
    if (g_et4k_vram_window) {
        return 1;
    }
    if (phys_base != VGA_WINDOW_PHYS) {
        et4k_log("map_vram_window: unsupported base requested");
        return 0;
    }
    g_et4k_vram_window = (volatile uint8_t*)(uintptr_t)phys_base;
    if (et4k_trace_enabled()) {
        et4k_log_hex("map_vram_window.phys", phys_base);
    }
    return 1;
}

static void et4k_shadow_upload(const et4k_fb_state_t* state) {
    if (!state || !state->buffer || state->width == 0 || state->height == 0) {
        et4k_log("shadow_upload: skipped (invalid state)");
        return;
    }

    if (!g_et4k_vram_window) {
        et4k_log("shadow_upload: skipped (VRAM window not mapped)");
        return;
    }

    et4k_wait_vblank_window();

    uint32_t irq_flags = et4k_irq_guard_acquire();

    const uint16_t width = state->width;
    const uint16_t height = state->height;
    const uint32_t pitch = state->pitch;
    const uint32_t bytes_per_line = width / 8u;
    volatile uint8_t* vram = g_et4k_vram_window;

    if (et4k_trace_enabled()) {
        et4k_log("shadow_upload: begin");
        et4k_log_dec("shadow.width", width);
        et4k_log_dec("shadow.height", height);
        et4k_log_dec("shadow.pitch", pitch);
        et4k_log_hex("shadow.vram_base", VGA_WINDOW_PHYS);
    }

    for (uint8_t plane = 0; plane < 4; ++plane) {
        if (et4k_trace_enabled()) {
            console_write("[et4k] shadow_upload: plane=");
            console_write_dec(plane);
            console_write("\n");
        }
        et4k_seq_write(0x02, (uint8_t)(1u << plane));
        et4k_gc_write(0x04, plane);
        if (et4k_trace_enabled()) {
            et4k_log_reg_write("SEQ_PLANE", 0x02, (uint8_t)(1u << plane));
            et4k_log_reg_write("GC_PLANE", 0x04, plane);
        }

        for (uint16_t y = 0; y < height; ++y) {
            const uint8_t* src_line = state->buffer + (uint32_t)y * pitch;
            volatile uint8_t* dst_line = vram + (uint32_t)y * bytes_per_line;
            for (uint32_t byte_index = 0; byte_index < bytes_per_line; ++byte_index) {
                uint8_t packed = 0;
                const uint32_t pixel_base = byte_index * 8u;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    uint8_t pixel = src_line[pixel_base + bit] & 0x0F;
                    if (pixel & (1u << plane)) {
                        packed |= (uint8_t)(0x80u >> bit);
                    }
                }
                et4k_serial_heartbeat('<');
                dst_line[byte_index] = packed;
                et4k_serial_heartbeat('>');
            }
        }
    }

    et4k_seq_write(0x02, 0x0F);
    et4k_gc_write(0x08, 0xFF);
    if (et4k_trace_enabled()) {
        et4k_log_reg_write("SEQ_PLANE", 0x02, 0x0F);
        et4k_log_reg_write("GC_BITMASK", 0x08, 0xFF);
    }
    et4k_log("shadow_upload: end");

    et4k_irq_guard_release(irq_flags);
}

static int et4k_fb_fill_rect(void* ctx, uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height, uint8_t color) {
    et4k_fb_state_t* state = (et4k_fb_state_t*)ctx;
    if (!state || !state->buffer || width == 0 || height == 0) {
        et4k_log("fb_fill_rect: rejected (invalid state or zero dims)");
        return 0;
    }
    if (x >= state->width || y >= state->height) {
        et4k_log("fb_fill_rect: rejected (outside bounds)");
        return 0;
    }
    uint16_t max_width = (uint16_t)(state->width - x);
    uint16_t max_height = (uint16_t)(state->height - y);
    if (width > max_width) width = max_width;
    if (height > max_height) height = max_height;

    if (et4k_trace_enabled()) {
        console_write("[et4k] fb_fill_rect: x=");
        console_write_dec(x);
        console_write(" y=");
        console_write_dec(y);
        console_write(" w=");
        console_write_dec(width);
        console_write(" h=");
        console_write_dec(height);
        console_write(" color=0x");
        console_write_hex32(color);
        console_write("\n");
    }

    for (uint16_t row = 0; row < height; ++row) {
        uint8_t* dst = state->buffer + (uint32_t)(y + row) * state->pitch + x;
        for (uint16_t col = 0; col < width; ++col) {
            dst[col] = (uint8_t)(color & 0x0F);
        }
    }
    state->dirty = 1;
    return 1;
}

static void et4k_fb_mark_dirty(void* ctx, uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height) {
    (void)x; (void)y; (void)width; (void)height;
    et4k_fb_state_t* state = (et4k_fb_state_t*)ctx;
    if (!state) return;
    if (et4k_trace_enabled()) {
        console_write("[et4k] fb_mark_dirty: x=");
        console_write_dec(x);
        console_write(" y=");
        console_write_dec(y);
        console_write(" w=");
        console_write_dec(width);
        console_write(" h=");
        console_write_dec(height);
        console_write("\n");
    }
    state->dirty = 1;
}

static void et4k_fb_sync(void* ctx) {
    et4k_fb_state_t* state = (et4k_fb_state_t*)ctx;
    if (!state) {
        et4k_log("fb_sync: skipped (null state)");
        return;
    }
    if (!state->dirty) {
        et4k_log("fb_sync: skipped (clean)");
        return;
    }
    et4k_log("fb_sync: uploading shadow buffer");
    et4k_shadow_upload(state);
    state->dirty = 0;
    et4k_log("fb_sync: complete");
}

static void et4k_load_palette16(void) {
    et4k_log("load_palette16: begin");
    outb(0x3C8, 0x00);
    et4k_io_wait();
    et4k_log_hex("DAC_ADDR", 0x00);
    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t r = k_palette16[i][0];
        uint8_t g = k_palette16[i][1];
        uint8_t b = k_palette16[i][2];
        outb(0x3C9, r);
        et4k_io_wait();
        outb(0x3C9, g);
        et4k_io_wait();
        outb(0x3C9, b);
        et4k_io_wait();
        if (et4k_trace_enabled()) {
            console_write("[et4k] PALETTE16[");
            console_write_dec(i);
            console_write("]=(");
            console_write_dec(r);
            console_write(",");
            console_write_dec(g);
            console_write(",");
            console_write_dec(b);
            console_write(")\n");
        }
    }
    et4k_log("load_palette16: end");
}

static void et4k_program_mode12(void) {
    et4k_log("program_mode12: enter");
    uint32_t irq_flags = et4k_irq_guard_acquire();
    uint8_t saved_crt11 = et4k_crtc_read(0x11);
    et4k_log_hex("CRTC[0x11]_saved", saved_crt11);

    et4k_misc_write(0xE3);
    et4k_log_hex("MISC_WRITE", 0xE3);

    et4k_log("program_mode12: asserting SR0 reset");
    et4k_seq_write(0x00, 0x00);
    et4k_log_reg_write("SEQ", 0x00, 0x00);
    et4k_log("program_mode12: SR0 asserted");
    for (uint8_t i = 1; i < 5; ++i) {
        et4k_seq_write(i, k_seq_mode12[i]);
        et4k_log_reg_write("SEQ12", i, k_seq_mode12[i]);
    }
    et4k_log("program_mode12: sequencer core programmed");
    et4k_seq_write(0x00, 0x03);
    et4k_log_reg_write("SEQ", 0x00, 0x03);
    et4k_log("program_mode12: SR0 released");

    et4k_crtc_write(0x11, (uint8_t)(saved_crt11 & (uint8_t)~0x80u));
    et4k_log_reg_write("CRTC12", 0x11, (uint8_t)(saved_crt11 & (uint8_t)~0x80u));
    for (uint8_t i = 0; i < 25; ++i) {
        et4k_crtc_write(i, k_crtc_mode12[i]);
        et4k_log_reg_write("CRTC12", i, k_crtc_mode12[i]);
    }
    et4k_crtc_write(0x11, saved_crt11);
    et4k_log_reg_write("CRTC12", 0x11, saved_crt11);
    et4k_log("program_mode12: CRTC block programmed");

    for (uint8_t i = 0; i < 9; ++i) {
        et4k_gc_write(i, k_graph_mode12[i]);
        et4k_log_reg_write("GC12", i, k_graph_mode12[i]);
    }
    et4k_log("program_mode12: GC block done");

    for (uint8_t i = 0; i < 21; ++i) {
        et4k_attr_write(i, k_attr_mode12[i]);
        et4k_log_reg_write("ATTR12", i, k_attr_mode12[i]);
    }
    et4k_log("program_mode12: ATC palette done");
    vga_attr_reenable_video();
    et4k_log("program_mode12: ATC video enable on");

    vga_pel_mask_write(0xFF);
    et4k_io_wait();
    et4k_log_hex("PEL_MASK", 0xFF);
    et4k_load_palette16();
    et4k_log("program_mode12: exit");
    et4k_irq_guard_release(irq_flags);
}

static void et4k_program_text_mode(void) {
    et4k_log("program_text_mode: enter");
    uint32_t irq_flags = et4k_irq_guard_acquire();
    uint8_t saved_crt11 = et4k_crtc_read(0x11);
    et4k_log_hex("CRTC[0x11]_saved", saved_crt11);

    et4k_misc_write(0x67);
    et4k_log_hex("MISC_WRITE", 0x67);

    et4k_log("program_text_mode: asserting SR0 reset");
    et4k_seq_write(0x00, 0x00);
    et4k_log_reg_write("SEQ", 0x00, 0x00);
    et4k_log("program_text_mode: SR0 asserted");
    for (uint8_t i = 1; i < 5; ++i) {
        et4k_seq_write(i, k_seq_text[i]);
        et4k_log_reg_write("SEQTXT", i, k_seq_text[i]);
    }
    et4k_log("program_text_mode: sequencer core programmed");
    et4k_seq_write(0x00, 0x03);
    et4k_log_reg_write("SEQ", 0x00, 0x03);
    et4k_log("program_text_mode: SR0 released");

    et4k_crtc_write(0x11, (uint8_t)(saved_crt11 & (uint8_t)~0x80u));
    et4k_log_reg_write("CRTCTX", 0x11, (uint8_t)(saved_crt11 & (uint8_t)~0x80u));
    for (uint8_t i = 0; i < 25; ++i) {
        et4k_crtc_write(i, k_crtc_text[i]);
        et4k_log_reg_write("CRTCTX", i, k_crtc_text[i]);
    }
    et4k_crtc_write(0x11, saved_crt11);
    et4k_log_reg_write("CRTCTX", 0x11, saved_crt11);
    et4k_log("program_text_mode: CRTC block programmed");

    for (uint8_t i = 0; i < 9; ++i) {
        et4k_gc_write(i, k_graph_text[i]);
        et4k_log_reg_write("GRAPHTX", i, k_graph_text[i]);
    }
    et4k_log("program_text_mode: GC block done");
    for (uint8_t i = 0; i < 21; ++i) {
        et4k_attr_write(i, k_attr_text[i]);
        et4k_log_reg_write("ATTRTX", i, k_attr_text[i]);
    }
    et4k_log("program_text_mode: ATC palette done");
    vga_attr_reenable_video();
    et4k_log("program_text_mode: ATC video enable on");

    vga_pel_mask_write(0xFF);
    et4k_io_wait();
    et4k_log_hex("PEL_MASK", 0xFF);
    vga_dac_reset_text_palette();
    et4k_log("program_text_mode: exit");
    et4k_irq_guard_release(irq_flags);
}

int et4000_detect(gpu_info_t* out_info) {
    et4k_log("detect: enter et4000_detect");
    if (!out_info) {
        et4k_log("detect: null out_info pointer");
        return 0;
    }

    et4k_bzero(out_info, (uint32_t)sizeof(*out_info));
    g_is_ax_variant = 0;

    uint32_t irq_flags = et4k_irq_guard_acquire();
    int result = 0;

    uint8_t ext_before = inb(ET4K_EXT_PORT);
    io_delay();
    uint8_t ext_unlock = (uint8_t)(ext_before | 0x03u);
    et4k_log_hex("detect.ext_before", ext_before);
    et4k_log_hex("detect.ext_unlock", ext_unlock);
    if (ext_unlock != ext_before) {
        outb(ET4K_EXT_PORT, ext_unlock);  // enable Tseng extended register window
        io_delay();
        et4k_log("detect: unlock applied to 0x3BF");
    } else {
        et4k_log("detect: 0x3BF already unlocked");
    }

    uint8_t sig_before = inb(ET4K_SIGNATURE_PORT);
    io_delay();
    et4k_log_hex("detect.sig_before", sig_before);
    outb(ET4K_SIGNATURE_PORT, 0x55);
    io_delay();
    et4k_log("detect: wrote 0x55 to 0x3CD");
    uint8_t sig_read1 = inb(ET4K_SIGNATURE_PORT);
    io_delay();
    et4k_log_hex("detect.sig_read1", sig_read1);
    outb(ET4K_SIGNATURE_PORT, 0xAA);
    io_delay();
    et4k_log("detect: wrote 0xAA to 0x3CD");
    uint8_t sig_read2 = inb(ET4K_SIGNATURE_PORT);
    io_delay();
    et4k_log_hex("detect.sig_read2", sig_read2);

    outb(ET4K_SIGNATURE_PORT, sig_before);
    io_delay();
    outb(ET4K_EXT_PORT, ext_before);
    io_delay();
    et4k_log("detect: restored 0x3CD/0x3BF");

    int signature_ok = (sig_read1 == 0x55) && (sig_read2 == 0xAA);
    int fallback_ok = 0;
    et4k_log(signature_ok ? "detect: signature matched" : "detect: signature mismatch");

    if (!signature_ok) {
        uint8_t crtc_before = et4k_crtc_read(0x33);
        uint8_t crtc_toggle = (uint8_t)(crtc_before ^ 0x0F);
        et4k_crtc_write(0x33, crtc_toggle);
        uint8_t crtc_after = et4k_crtc_read(0x33);
        fallback_ok = ((crtc_after & 0x0F) == (crtc_toggle & 0x0F));
        et4k_crtc_write(0x33, crtc_before);
        if (et4k_trace_enabled()) {
            console_write("[et4k] detect.fallback.crtc_before=0x");
            console_write_hex32(crtc_before);
            console_write(" toggle=0x");
            console_write_hex32(crtc_toggle);
            console_write(" after=0x");
            console_write_hex32(crtc_after);
            console_write("\n");
        }
        if (fallback_ok) {
            gpu_debug_log("WARN", "et4k: signature mismatch, CRTC[33] latch toggled (probable ET4000)");
            et4k_log("detect: fallback latch succeeded");
        } else {
            et4k_log("detect: fallback latch failed");
        }
    }

    g_et4k_detect.detected = signature_ok || fallback_ok;
    g_et4k_detect.signature_ok = signature_ok;
    et4k_log_dec("detect.detected", (uint32_t)g_et4k_detect.detected);
    et4k_log_dec("detect.signature_ok", (uint32_t)g_et4k_detect.signature_ok);

    if (!g_et4k_detect.detected) {
        gpu_set_last_error("ERROR: Tseng ET4000 signature mismatch");
        gpu_debug_log("ERROR", "et4k: signature test via port 3CDh failed");
        et4k_log("detect: exit failure");
        goto cleanup;
    }

    if (!signature_ok && fallback_ok) {
        gpu_set_last_error("WARN: Tseng ET4000 detected via CRTC fallback");
        et4k_log("detect: proceeding with fallback-detected adapter");
    }

    out_info->type = GPU_TYPE_ET4000;
    et4k_copy_string(out_info->name, (uint32_t)sizeof(out_info->name), "Tseng ET4000");
    out_info->framebuffer_bar = 0xFF;
    out_info->framebuffer_base = VGA_WINDOW_PHYS;
    out_info->framebuffer_size = VGA_WINDOW_SIZE;
    out_info->capabilities = 0;
    if (et4k_trace_enabled()) {
        console_write("[et4k] detect.out_info: base=0x");
        console_write_hex32(out_info->framebuffer_base);
        console_write(" size=0x");
        console_write_hex32(out_info->framebuffer_size);
        console_write(" caps=0x");
        console_write_hex32(out_info->capabilities);
        console_write("\n");
    }

    if (signature_ok) {
        gpu_debug_log("INFO", "et4k: legacy Tseng ET4000 detected");
        gpu_set_last_error("OK: Tseng ET4000 detected");
        et4k_log("detect: exit OK");
    } else {
        gpu_debug_log("WARN", "et4k: legacy Tseng ET4000 detected via CRTC fallback");
        et4k_log("detect: exit WARN (fallback)");
    }

    result = 1;

cleanup:
    et4k_irq_guard_release(irq_flags);
    return result;
}

static int et4k_ensure_detected(void) {
    if (g_et4k_detect.detected) {
        et4k_log("ensure_detected: already detected");
        return 1;
    }
    et4k_log("ensure_detected: detection required, invoking et4000_detect");
    gpu_info_t info;
    int ok = et4000_detect(&info);
    et4k_log_dec("ensure_detected.result", (uint32_t)ok);
    return ok;
}

int et4k_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode,
                  uint16_t width, uint16_t height, uint8_t bpp) {
    if (et4k_trace_enabled()) {
        console_write("[et4k] set_mode: gpu=");
        console_write_hex32((uint32_t)(uintptr_t)gpu);
        console_write(" out_mode=");
        console_write_hex32((uint32_t)(uintptr_t)out_mode);
        console_write(" requested=");
        console_write_dec(width);
        console_write("x");
        console_write_dec(height);
        console_write("x");
        console_write_dec(bpp);
        console_write("\n");
    }
    if (!gpu || !out_mode) {
        et4k_log("set_mode: invalid arguments");
        return 0;
    }
    if (width != 640 || height != 480 || bpp != 4) {
        gpu_set_last_error("ERROR: unsupported Tseng mode (requires 640x480x4)");
        et4k_log("set_mode: rejected unsupported mode");
        return 0;
    }
    if (!et4k_ensure_detected()) {
        et4k_log("set_mode: detection failed");
        return 0;
    }

    et4k_log("set_mode: programming VGA mode 12h");
    et4k_program_mode12();
    et4k_log("set_mode: mode12 staged OK (pre-framebuffer)");

    et4k_log("set_mode: resetting Tseng window registers to bank 0");
    et4k_reset_window_registers();

    if (!et4k_map_vram_window(VGA_WINDOW_PHYS)) {
        et4k_log("set_mode: VRAM window mapping failed");
        return 0;
    }

    et4k_log("set_mode: initializing shadow framebuffer");
    et4k_bzero(g_et4k_shadow, (uint32_t)sizeof(g_et4k_shadow));
    g_et4k_fb.buffer = g_et4k_shadow;
    g_et4k_fb.width = 640;
    g_et4k_fb.height = 480;
    g_et4k_fb.pitch = 640;
    g_et4k_fb.bpp = 8;
    g_et4k_fb.dirty = 1;
    if (et4k_trace_enabled()) {
        console_write("[et4k] set_mode.shadow buffer=0x");
        console_write_hex32((uint32_t)(uintptr_t)g_et4k_fb.buffer);
        console_write(" size=");
        console_write_dec((uint32_t)sizeof(g_et4k_shadow));
        console_write("\n");
    }
    et4k_fb_mark_dirty(&g_et4k_fb, 0, 0, g_et4k_fb.width, g_et4k_fb.height);
    et4k_fb_sync(&g_et4k_fb);

    et4k_log("set_mode: registering framebuffer accelerator");
    fb_accel_register(&g_et4k_fb_ops, &g_et4k_fb);
    et4k_log("set_mode: framebuffer accelerator registered");

    gpu->framebuffer_width = 640;
    gpu->framebuffer_height = 480;
    gpu->framebuffer_pitch = 640;
    gpu->framebuffer_bpp = 8;
    gpu->framebuffer_ptr = g_et4k_shadow;
    gpu->framebuffer_size = VGA_WINDOW_SIZE;
    if (et4k_trace_enabled()) {
        console_write("[et4k] set_mode.gpu fb_base=0x");
        console_write_hex32(gpu->framebuffer_base);
        console_write(" fb_size=0x");
        console_write_hex32(gpu->framebuffer_size);
        console_write(" ptr=0x");
        console_write_hex32((uint32_t)(uintptr_t)gpu->framebuffer_ptr);
        console_write("\n");
    }

    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    out_mode->width = 640;
    out_mode->height = 480;
    out_mode->bpp = 8;
    out_mode->pitch = 640;
    out_mode->phys_base = gpu->framebuffer_base ? gpu->framebuffer_base : VGA_WINDOW_PHYS;
    out_mode->framebuffer = g_et4k_shadow;
    if (et4k_trace_enabled()) {
        console_write("[et4k] set_mode.out_mode phys=0x");
        console_write_hex32(out_mode->phys_base);
        console_write(" pitch=");
        console_write_dec(out_mode->pitch);
        console_write(" framebuffer=0x");
        console_write_hex32((uint32_t)(uintptr_t)out_mode->framebuffer);
        console_write("\n");
    }

    gpu_set_last_error("OK: Tseng VGA mode 12h active");
    gpu_debug_log("OK", "et4k: programmed VGA mode 12h (640x480x16 colors)");
    et4k_log("set_mode: completed successfully");
    return 1;
}

int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode) {
    if (et4k_trace_enabled()) {
        console_write("[et4k] et4000_set_mode: mode=");
        console_write_dec((uint32_t)mode);
        console_write("\n");
    }
    switch (mode) {
        case ET4000_MODE_640x480x4:
            {
                int ok = et4k_set_mode(gpu, out_mode, 640, 480, 4);
                et4k_log_dec("et4000_set_mode.result", (uint32_t)ok);
                return ok;
            }
        default:
            gpu_set_last_error("ERROR: requested Tseng mode requires AX extensions");
            et4k_log("et4000_set_mode: unsupported mode requested");
            return 0;
    }
}

et4000_mode_t et4k_choose_default_mode(int is_ax_variant, uint32_t vram_bytes) {
    if (et4k_trace_enabled()) {
        console_write("[et4k] choose_default_mode: is_ax=");
        console_write_dec((uint32_t)is_ax_variant);
        console_write(" vram_bytes=0x");
        console_write_hex32(vram_bytes);
        console_write("\n");
    }
    (void)is_ax_variant; (void)vram_bytes;
    return ET4000_MODE_640x480x4;
}

void et4000_restore_text_mode(void) {
    et4k_log("restore_text_mode: enter");
    et4k_program_text_mode();
    et4k_log("restore_text_mode: text core staged OK");
    g_et4k_vram_window = NULL;
    g_et4k_fb.buffer = NULL;
    g_et4k_fb.width = 0;
    g_et4k_fb.height = 0;
    g_et4k_fb.pitch = 0;
    g_et4k_fb.bpp = 0;
    g_et4k_fb.dirty = 0;
    fb_accel_reset();
    gpu_set_last_error("OK: VGA text mode restored");
    gpu_debug_log("INFO", "et4k: restored 80x25 text mode");
    et4k_log("restore_text_mode: exit");
}

void et4000_dump_bank(uint8_t bank, uint32_t offset, uint32_t length) {
    (void)bank; (void)offset; (void)length;
    et4k_log("dump_bank: not supported");
    console_writeln("et4000-dump: banking disabled for legacy ISA mode.");
}

int et4000_capture_dump(uint8_t bank, uint32_t offset, uint32_t length) {
    (void)bank; (void)offset; (void)length;
    et4k_log("capture_dump: not supported");
    console_writeln("et4000-capture: unsupported without AX extensions.");
    return 0;
}

void et4000_debug_dump(void) {
    et4k_log("debug_dump: enter");
    console_writeln("et4000-debug: legacy ET4000 detection summary");
    console_write("  signature: ");
    console_writeln(g_et4k_detect.signature_ok ? "OK" : "FAIL");
    et4k_log("debug_dump: exit");
}

void et4000_set_debug_trace(int enabled) {
    g_et4k_debug_trace = enabled ? 1 : 0;
    gpu_debug_log("INFO", enabled ? "et4k: debug trace enabled" : "et4k: debug trace disabled");
    et4k_log(enabled ? "trace: enabled" : "trace: disabled");
}

int et4k_debug_trace_enabled(void) {
    if (et4k_trace_enabled()) {
        et4k_log("trace_enabled: queried");
    }
    return g_et4k_debug_trace;
}

void et4k_disable_ax_engine(const char* reason) {
    (void)reason;
    // No AX engine active in legacy mode.
    if (et4k_trace_enabled()) {
        console_write("[et4k] disable_ax_engine: reason=");
        console_write(reason ? reason : "null");
        console_write("\n");
    }
}

int et4k_detection_toggle_ok(void) {
    et4k_log_dec("detect.toggle_ok", (uint32_t)g_et4k_detect.detected);
    return g_et4k_detect.detected;
}

int et4k_detection_latch_ok(void) {
    et4k_log_dec("detect.latch_ok", (uint32_t)g_et4k_detect.detected);
    return g_et4k_detect.detected;
}

int et4k_detection_signature_ok(void) {
    et4k_log_dec("detect.signature_ok_cached", (uint32_t)g_et4k_detect.signature_ok);
    return g_et4k_detect.signature_ok;
}

int et4k_detection_alias_limited(void) {
    et4k_log("detect.alias_limited: returning 0");
    return 0;
}

uint32_t et4k_detection_alias_limit_bytes(void) {
    et4k_log("detect.alias_limit_bytes: returning 0");
    return 0;
}

#endif // CONFIG_ARCH_X86
