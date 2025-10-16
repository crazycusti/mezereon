#include "et4000.h"
#include "vga_hw.h"
#include "fb_accel.h"
#include "et4000_common.h"
#include "../../config.h"
#include "../../console.h"
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

int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode) {
    (void)gpu;
    (void)out_mode;
    (void)mode;
    return 0;  // Nicht-x86 Plattform
}

void et4000_restore_text_mode(void) {
    // Nicht-x86 Plattform
}
#else

#include "et4000ax.h"
#include "et4000_ax_detect.h"

#define ET4K_BANK_PORT    0x3CD
#define ET4K_WINDOW_PORT  0x3CB
#define ET4K_EXT_PORT     0x3BF
#define ET4K_WINDOW_PHYS  0xA0000u
#define ET4K_BANK_SIZE    0x10000u

/*
 * NOTE: Legacy ET4000 ISA boards often rely on strap or jumper settings for
 * wait-states. The driver assumes default timings; if the physical board
 * shows instability, adjust the hardware configuration accordingly.
 */

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
    uint32_t dirty_start_offset;
    uint32_t dirty_end_offset;
} et4k_state_t;

static et4k_state_t g_et4k = {0};
static uint8_t g_ext_port_saved = 0;
static uint8_t g_ext_port_active = 0;
static int g_ext_port_saved_valid = 0;
static int g_et4k_debug_trace = 0;
static uint8_t g_et4k_saved_bank = 0;
static uint8_t g_et4k_saved_window = 0;
static int g_et4k_saved_state_valid = 0;
static uint8_t g_et4k_bank_read = 0;
static uint8_t g_et4k_bank_write = 0;
static int g_et4k_ax_engine_ready = 0;
static int et4k_verify_vram_window(void);

static inline void et4k_io_delay(void) {
    outb(0x80, 0);
}

static inline uint8_t et4k_atc_read(uint8_t index);
static inline void et4k_atc_write(uint8_t index, uint8_t value);
static inline void et4k_atc_video_disable(void);
static inline void et4k_atc_video_enable(void);

static void et4k_trace(const char* msg) {
    if (!g_et4k_debug_trace || !msg) return;
    console_write("    [et4k] ");
    console_writeln(msg);
}

static void et4k_trace_hex(const char* label, uint32_t value) {
    if (!g_et4k_debug_trace || !label) return;
    console_write("    [et4k] ");
    console_write(label);
    console_write("=0x");
    console_write_hex32(value);
    console_write("\n");
}

static void et4k_write_key_sequence(void) {
    if (g_et4k_debug_trace) {
        console_writeln("    [et4k] writing ATC key sequence");
    }
    outb(0x3BF, 0x03);
    et4k_io_delay();
    outb(0x3D8, 0xA0);
    et4k_io_delay();
    vga_attr_reenable_video();
}

static inline uint8_t et4k_atc_read(uint8_t index) {
#if CONFIG_ARCH_X86
    (void)inb(0x3DA);
    outb(0x3C0, (uint8_t)(index | 0x20u));
    return inb(0x3C1);
#else
    (void)index;
    return 0;
#endif
}

static inline void et4k_atc_video_disable(void) {
#if CONFIG_ARCH_X86
    uint8_t mode = et4k_atc_read(0x10);
    et4k_atc_write(0x10, (uint8_t)(mode | 0x20u));
#endif
}

static inline void et4k_atc_video_enable(void) {
#if CONFIG_ARCH_X86
    uint8_t mode = et4k_atc_read(0x10);
    et4k_atc_write(0x10, (uint8_t)(mode & (uint8_t)~0x20u));
    vga_attr_reenable_video();
#endif
}

static inline void et4k_atc_write(uint8_t index, uint8_t value) {
#if CONFIG_ARCH_X86
    (void)inb(0x3DA);
    outb(0x3C0, index);
    outb(0x3C0, value);
#else
    (void)index; (void)value;
#endif
}


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
    0xEA,0x0C,0xDF,0x50,0x00,0xE7,0x04,0xE3,
    0xFF
};
static const uint8_t std_graph_640x480[9] = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
static const uint8_t std_attr_640x480[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x41,0x00,0x0F,0x00,0x00
};

static const uint8_t std_seq_640x400[5] = { 0x03,0x01,0x0F,0x00,0x06 };
static const uint8_t std_crtc_640x400[25] = {
    0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,
    0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
    0x9C,0x8E,0x8F,0x50,0x1F,0x96,0xB9,0xA3,
    0xFF
};
static const uint8_t std_graph_640x400[9] = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
static const uint8_t std_attr_640x400[21] = {
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

static uint8_t et4k_unlock_value(uint8_t ext) {
    if (ext == 0xFF || ext == 0x00) {
        return 0x03u;
    }
    return (uint8_t)((ext & 0xFCu) | 0x03u);
}

static void et4k_set_window_raw(uint8_t value) {
    outb(ET4K_WINDOW_PORT, value);
    et4k_io_delay();
    if (g_et4k_debug_trace) {
        console_write("    [et4k] window=0x");
        console_write_hex32(value);
        console_write("\n");
    }
}

static void et4k_set_bank_rw(uint8_t read_bank, uint8_t write_bank) {
    uint8_t value = (uint8_t)((read_bank & 0x0Fu) | ((write_bank & 0x0Fu) << 4));
    outb(ET4K_BANK_PORT, value);
    et4k_io_delay();
    g_et4k_bank_read = (uint8_t)(read_bank & 0x0Fu);
    g_et4k_bank_write = (uint8_t)(write_bank & 0x0Fu);
    if (g_et4k_debug_trace) {
        console_write("    [et4k] bank rw=");
        console_write_hex32(value);
        console_write(" (r=");
        console_write_hex32(g_et4k_bank_read);
        console_write(", w=");
        console_write_hex32(g_et4k_bank_write);
        console_write(")\n");
    }
}

static void et4k_set_bank(uint8_t bank) {
    et4k_set_bank_rw(bank, bank);
}

static int et4k_verify_vram_window(void) {
    uint8_t saved_window = inb(ET4K_WINDOW_PORT);
    uint8_t saved_bank = inb(ET4K_BANK_PORT);
    et4k_set_window_raw(0x00);
    et4k_set_bank(0);

    volatile uint8_t* window = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;
    uint8_t original = window[0];
    uint8_t probe = (uint8_t)(original ^ 0x5Au);
    window[0] = probe;
    et4k_io_delay();
    uint8_t observed = window[0];
    window[0] = original;
    et4k_io_delay();

    uint8_t restore_read = (uint8_t)(saved_bank & 0x0Fu);
    uint8_t restore_write = (uint8_t)((saved_bank >> 4) & 0x0Fu);
    et4k_set_bank_rw(restore_read, restore_write);
    et4k_set_window_raw(saved_window);

    return observed == probe;
}

static void et4k_enable_extensions(void) {
    et4k_write_key_sequence();
    uint8_t ext = inb(ET4K_EXT_PORT);
    if (!g_ext_port_saved_valid) {
        g_ext_port_saved = ext;
        g_ext_port_saved_valid = 1;
    }
    et4k_trace_hex("read ext", ext);
    uint8_t new_ext = et4k_unlock_value(ext);
    if (new_ext != g_ext_port_active || ext == 0xFF || ext == 0x00) {
        outb(ET4K_EXT_PORT, new_ext);
        et4k_io_delay();
        g_ext_port_active = new_ext;
        et4k_trace_hex("write ext", new_ext);
    } else {
        g_ext_port_active = new_ext;
    }
}

static void et4k_restore_extensions(void) {
    if (g_ext_port_saved_valid && g_ext_port_active != g_ext_port_saved) {
        outb(ET4K_EXT_PORT, g_ext_port_saved);
        et4k_io_delay();
        g_ext_port_active = g_ext_port_saved;
        et4k_trace_hex("restore ext", g_ext_port_saved);
    }
}

static void et4k_configure_variant_registers(void) {
    et4k_atc_video_disable();

    if (g_is_ax_variant) {
        et4k_trace("configuring ET4000AX extended bits");
        g_et4k_ax_engine_ready = et4kax_after_modeset_init();
        if (!g_et4k_ax_engine_ready && g_et4k_debug_trace) {
            console_writeln("    [et4k-ax] accel engine not ready; falling back to CPU path");
        }
    } else {
        et4k_trace("configuring ET4000 (non-AX) defaults");
        uint8_t attr16 = et4k_atc_read(0x16);
        et4k_atc_write(0x16, (uint8_t)(attr16 & (uint8_t)~0x40u));

        uint8_t seq7 = vga_seq_read(0x07);
        vga_seq_write(0x07, (uint8_t)(seq7 & (uint8_t)~0x10u));

        vga_crtc_write(0x32, 0x00u);
        g_et4k_ax_engine_ready = 0;
    }

    et4k_set_window_raw(0x00);
    et4k_set_bank(0);
    et4k_atc_video_enable();
}

int detect_et4000ax(void) {
    et4k_enable_extensions();
    uint8_t original_ctrl = inb(0x3C3);
    if (g_et4k_debug_trace) {
        console_write("    [et4k-ax] ctrl orig=0x");
        console_write_hex32(original_ctrl);
        console_write("\n");
    }

    uint8_t candidates[3];
    candidates[0] = (uint8_t)(original_ctrl | 0x20u);
    candidates[1] = 0x23u;
    candidates[2] = 0x20u;

    uint8_t ctrl_after = original_ctrl;
    int bit_set = (ctrl_after & 0x20u) != 0;
    for (int i = 0; i < 3 && !bit_set; i++) {
        uint8_t val = candidates[i];
        if (val == ctrl_after) continue;
        outb(0x3C3, val);
        et4k_io_delay();
        ctrl_after = inb(0x3C3);
        if (g_et4k_debug_trace) {
            console_write("    [et4k-ax] ctrl write 0x");
            console_write_hex32(val);
            console_write(" -> 0x");
            console_write_hex32(ctrl_after);
            console_write("\n");
        }
        if (ctrl_after & 0x20u) {
            bit_set = 1;
        }
    }

    outb(0x3C3, original_ctrl);
    et4k_io_delay();

    if (!bit_set) {
        if (g_et4k_debug_trace) {
            console_writeln("    [et4k-ax] control bit 5 not latched; assuming non-AX");
        }
        return 0;
    }

    outb(ET4K_AX_ACCEL_CMD, 0x00);
    et4k_io_delay();
    uint8_t status = inb(ET4K_AX_ACCEL_STATUS);
    if (g_et4k_debug_trace) {
        console_write("    [et4k-ax] accel status=0x");
        console_write_hex32(status);
        console_write("\n");
    }
    if (status == 0xFF || status == 0x00) {
        if (g_et4k_debug_trace) {
            console_writeln("    [et4k-ax] accel status suspicious, treating as non-AX");
        }
        return 0;
    }

    if (!(status & ET4K_AX_STATUS_READY)) {
        for (int t = 0; t < 10000 && !(status & ET4K_AX_STATUS_READY); ++t) {
            status = inb(ET4K_AX_ACCEL_STATUS);
            if (status == 0xFF || status == 0x00) {
                if (g_et4k_debug_trace) {
                    console_writeln("    [et4k-ax] accel status unstable, treating as non-AX");
                }
                return 0;
            }
        }
    }

    return (status & ET4K_AX_STATUS_READY) != 0;
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

static void et4k_mark_dirty_internal(uint32_t start_offset, uint32_t end_offset) {
    uint8_t first_bank = (uint8_t)(start_offset / ET4K_BANK_SIZE);
    uint8_t last_bank = (uint8_t)(end_offset / ET4K_BANK_SIZE);
    if (!g_et4k.dirty_pending) {
        g_et4k.dirty_first_bank = first_bank;
        g_et4k.dirty_last_bank = last_bank;
        g_et4k.dirty_pending = 1;
        g_et4k.dirty_start_offset = start_offset;
        g_et4k.dirty_end_offset = end_offset;
    } else {
        if (first_bank < g_et4k.dirty_first_bank) {
            g_et4k.dirty_first_bank = first_bank;
        }
        if (last_bank > g_et4k.dirty_last_bank) {
            g_et4k.dirty_last_bank = last_bank;
        }
        if (start_offset < g_et4k.dirty_start_offset) {
            g_et4k.dirty_start_offset = start_offset;
        }
        if (end_offset > g_et4k.dirty_end_offset) {
            g_et4k.dirty_end_offset = end_offset;
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

static int et4k_ax_fill_rect(void* ctx, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    int result = et4k_fill_rect(ctx, x, y, width, height, color);
    if (g_et4k_ax_engine_ready) {
        et4000ax_fill_rect((int)x, (int)y, (int)width, (int)height, color);
    }
    return result;
}

static void et4k_sync(void* ctx) {
    (void)ctx;
    if (!g_et4k.dirty_pending || !g_et4k.shadow) return;
    et4k_set_window_raw(0x00);
    uint32_t range_start = g_et4k.dirty_start_offset;
    uint32_t range_end = g_et4k.dirty_end_offset;
    uint8_t last = g_et4k.dirty_last_bank;
    for (uint8_t bank = g_et4k.dirty_first_bank; bank <= last; bank++) {
        uint32_t bank_base = (uint32_t)bank * ET4K_BANK_SIZE;
        if (bank_base >= g_et4k.shadow_size) break;
        uint32_t bank_limit = bank_base + ET4K_BANK_SIZE;
        uint32_t copy_start = (range_start > bank_base) ? (range_start - bank_base) : 0;
        uint32_t copy_end = (range_end + 1u > bank_limit) ? ET4K_BANK_SIZE : (range_end + 1u - bank_base);
        if (copy_end <= copy_start) continue;

        uint32_t copy = copy_end - copy_start;
        et4k_set_bank_rw(bank, bank);
        volatile uint8_t* dst = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS + copy_start;
        const uint8_t* src = g_et4k.shadow + bank_base + copy_start;
        for (uint32_t i = 0; i < copy; i++) {
            dst[i] = src[i];
        }
    }
    g_et4k.dirty_pending = 0;
}

static const fb_accel_ops_t g_et4k_ops_cpu = {
    .fill_rect = et4k_fill_rect,
    .sync = et4k_sync,
    .mark_dirty = et4k_mark_dirty,
};

static const fb_accel_ops_t g_et4k_ops_ax = {
    .fill_rect = et4k_ax_fill_rect,
    .sync = et4k_sync,
    .mark_dirty = et4k_mark_dirty,
};

int et4000_detect(gpu_info_t* out_info) {
    if (!out_info) return 0;
    g_is_ax_variant = 0;
    int detected = 0;
    et4k_trace("detect start");
    g_et4k_saved_state_valid = 0;
    uint8_t saved_ext = inb(ET4K_EXT_PORT);
    if (!g_ext_port_saved_valid) {
        g_ext_port_saved = saved_ext;
        g_ext_port_saved_valid = 1;
    }
    et4k_trace_hex("saved ext", saved_ext);
    g_ext_port_active = saved_ext;
    uint8_t saved_bank_reg = inb(ET4K_BANK_PORT);
    uint8_t saved_bank = (uint8_t)(saved_bank_reg & 0x0Fu);
    uint8_t saved_window = inb(ET4K_WINDOW_PORT);
    et4k_trace_hex("saved bank register", saved_bank_reg);
    et4k_trace_hex("saved bank", saved_bank);
    et4k_trace_hex("saved window", saved_window);
    g_et4k_bank_read = (uint8_t)(saved_bank_reg & 0x0Fu);
    g_et4k_bank_write = (uint8_t)((saved_bank_reg >> 4) & 0x0Fu);

    uint8_t unlocked_ext = et4k_unlock_value(saved_ext);
    if (unlocked_ext != saved_ext) {
        outb(ET4K_EXT_PORT, unlocked_ext);
        et4k_io_delay();
        g_ext_port_active = unlocked_ext;
        et4k_trace_hex("unlocked ext", unlocked_ext);
    } else {
        et4k_trace("extensions already unlocked");
    }
    outb(ET4K_BANK_PORT, 0x55);
    et4k_io_delay();
    uint8_t test1 = inb(ET4K_BANK_PORT);
    outb(ET4K_BANK_PORT, 0xAA);
    et4k_io_delay();
    uint8_t test2 = inb(ET4K_BANK_PORT);
    et4k_trace_hex("read 0x55", test1);
    et4k_trace_hex("read 0xAA", test2);

    if (test1 == 0x55 && test2 == 0xAA) {
        detected = 1;
        et4k_trace("signature matched");
    } else {
        et4k_trace("signature mismatch");
    }
    et4k_set_bank_rw(g_et4k_bank_read, g_et4k_bank_write);
    et4k_trace("segment/bank restored");

    if (!detected) {
        if (g_ext_port_active != saved_ext) {
            outb(ET4K_EXT_PORT, saved_ext);
            et4k_io_delay();
            g_ext_port_active = saved_ext;
        }
        et4k_set_bank_rw(g_et4k_bank_read, g_et4k_bank_write);
        et4k_set_window_raw(saved_window);
        et4k_trace("detect end (not found)");
        return 0;
    }

    et4k_bzero(out_info, sizeof(*out_info));
    g_is_ax_variant = detect_et4000ax();
    if (g_is_ax_variant) {
        out_info->type = GPU_TYPE_ET4000AX;
        et4k_copy_name(out_info->name, sizeof(out_info->name), "Tseng ET4000AX");
        out_info->capabilities |= GPU_CAP_ACCEL_2D;
    } else {
        out_info->type = GPU_TYPE_ET4000;
        et4k_copy_name(out_info->name, sizeof(out_info->name), "Tseng ET4000");
    }
    out_info->framebuffer_bar = 0xFF;
    out_info->framebuffer_base = ET4K_WINDOW_PHYS;
    out_info->framebuffer_size = sizeof(g_et4k_shadow);
    g_et4k_saved_bank = saved_bank_reg;
    g_et4k_saved_window = saved_window;
    g_et4k_saved_state_valid = 1;
    if (g_ext_port_active != saved_ext) {
        outb(ET4K_EXT_PORT, saved_ext);
        et4k_io_delay();
        g_ext_port_active = saved_ext;
    }
    et4k_trace("detect end (success)");
    return 1;
}

void et4000_set_debug_trace(int enabled) {
    g_et4k_debug_trace = enabled ? 1 : 0;
}

int et4k_debug_trace_enabled(void) {
    return g_et4k_debug_trace;
}

void et4k_disable_ax_engine(const char* reason) {
    if (!g_et4k_ax_engine_ready) return;
    g_et4k_ax_engine_ready = 0;
    if (g_et4k_debug_trace) {
        console_write("    [et4k-ax] accelerator disabled");
        if (reason && *reason) {
            console_write(": ");
            console_writeln(reason);
        } else {
            console_write("\n");
        }
    }
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
    g_et4k.dirty_start_offset = 0;
    g_et4k.dirty_end_offset = 0;
}

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  misc;
    const uint8_t* seq;
    const uint8_t* crtc;
    const uint8_t* graph;
    const uint8_t* attr;
} et4k_mode_desc_t;

static const et4k_mode_desc_t g_et4k_modes[] = {
    {
        640u, 480u, 640u, 8u, 0xE3,
        std_seq_640x480,
        std_crtc_640x480,
        std_graph_640x480,
        std_attr_640x480
    },
    {
        640u, 400u, 640u, 8u, 0xE3,
        std_seq_640x400,
        std_crtc_640x400,
        std_graph_640x400,
        std_attr_640x400
    }
};

void et4000_debug_dump(void) {
#if !CONFIG_ARCH_X86
    console_writeln("et4000-debug: not supported on this architecture.");
#else
    console_writeln("et4000-debug: probing ISA registers (3BFh/3CBh/3CDh)...");
    et4k_write_key_sequence();
    uint8_t saved_ext = inb(ET4K_EXT_PORT);
    uint8_t saved_bank_reg = inb(ET4K_BANK_PORT);
    uint8_t saved_window = inb(ET4K_WINDOW_PORT);

    console_write("  initial ext=");
    console_write_hex32(saved_ext);
    console_write(" bankReg=");
    console_write_hex32(saved_bank_reg);
    console_write(" window=");
    console_write_hex32(saved_window);
    console_write("\n");
    if (saved_ext == 0xFF) {
        console_writeln("    note: ext=0xFF indicates extensions disabled or floating bus");
    }
    if (saved_bank_reg == 0xFF) {
        console_writeln("    note: bank=0xFF suggests window unmapped; expect 0x00 after enabling");
    }

    uint8_t unlocked_ext = et4k_unlock_value(saved_ext);
    outb(ET4K_EXT_PORT, unlocked_ext);
    et4k_io_delay();
    uint8_t ext_after_unlock = inb(ET4K_EXT_PORT);
    console_write("  unlock write -> ext=");
    console_write_hex32(ext_after_unlock);
    console_write("\n");

    outb(ET4K_BANK_PORT, 0x55);
    et4k_io_delay();
    uint8_t seg_55 = inb(ET4K_BANK_PORT);
    outb(ET4K_BANK_PORT, 0xAA);
    et4k_io_delay();
    uint8_t seg_AA = inb(ET4K_BANK_PORT);
    console_write("  signature readback 0x55->");
    console_write_hex32(seg_55);
    console_write(", 0xAA->");
    console_write_hex32(seg_AA);
    console_write(seg_55 == 0x55 && seg_AA == 0xAA ? " (match)\n" : " (mismatch)\n");

    et4k_set_bank_rw((uint8_t)(saved_bank_reg & 0x0Fu), (uint8_t)((saved_bank_reg >> 4) & 0x0Fu));
    et4k_set_window_raw(saved_window);

    int ax_variant = detect_et4000ax();
    console_write("  AX accelerator status: ");
    console_write(ax_variant ? "present\n" : "not present\n");

    outb(ET4K_EXT_PORT, saved_ext);
    et4k_io_delay();
    console_writeln("et4000-debug: state restored.");
#endif
}

int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode) {
    if (!gpu || !out_mode) return 0;
    if (mode < ET4000_MODE_640x480x8 || mode > ET4000_MODE_640x400x8) return 0;

    const et4k_mode_desc_t* desc = &g_et4k_modes[mode];
    et4k_enable_extensions();

    vga_program_standard_mode(desc->misc, desc->seq, desc->crtc, desc->graph, desc->attr);
    vga_pel_mask_write(0xFF);
    vga_dac_load_default_palette();
    et4k_configure_variant_registers();

    if (!et4k_verify_vram_window()) {
        console_writeln("et4000: VRAM window test failed; aborting framebuffer mode.");
        et4k_disable_ax_engine("vram-window");
        et4000_restore_text_mode();
        return 0;
    }

    et4k_bzero(g_et4k_shadow, sizeof(g_et4k_shadow));

    g_et4k.width = desc->width;
    g_et4k.height = desc->height;
    g_et4k.pitch = desc->pitch;
    g_et4k.bpp = desc->bpp;
    g_et4k.shadow = g_et4k_shadow;
    g_et4k.shadow_size = (uint32_t)g_et4k.pitch * g_et4k.height;
    g_et4k.dirty_first_bank = 0;
    g_et4k.dirty_last_bank = (uint8_t)((g_et4k.shadow_size - 1u) / ET4K_BANK_SIZE);
    g_et4k.dirty_pending = 1;

    if (g_is_ax_variant) {
        fb_accel_register(&g_et4k_ops_ax, &g_et4k);
    } else {
        fb_accel_register(&g_et4k_ops_cpu, &g_et4k);
    }

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
    g_et4k_ax_engine_ready = 0;
    vga_program_standard_mode(0x67, std_seq_text, std_crtc_text, std_graph_text, std_attr_text);
    vga_dac_reset_text_palette();
    if (g_is_ax_variant) {
        uint8_t attr16 = vga_attr_read(0x16);
        vga_attr_write(0x16, (uint8_t)(attr16 & (uint8_t)~0x40u));
        vga_attr_reenable_video();

        uint8_t seq7 = vga_seq_read(0x07);
        vga_seq_write(0x07, (uint8_t)(seq7 & (uint8_t)~0x10u));

        vga_crtc_write(0x32, 0x00u);
        uint8_t c36 = vga_crtc_read(0x36);
        c36 &= (uint8_t)~0xC0u;
        vga_crtc_write(0x36, c36);
    }
    et4k_state_reset();
    et4k_restore_extensions();
    if (g_et4k_saved_state_valid) {
        uint8_t read_bank = (uint8_t)(g_et4k_saved_bank & 0x0Fu);
        uint8_t write_bank = (uint8_t)((g_et4k_saved_bank >> 4) & 0x0Fu);
        et4k_set_bank_rw(read_bank, write_bank);
        et4k_set_window_raw(g_et4k_saved_window);
    }
}

#endif // !CONFIG_ARCH_X86
