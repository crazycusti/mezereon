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

int et4k_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp) {
    (void)gpu; (void)out_mode; (void)width; (void)height; (void)bpp;
    return 0;
}

et4000_mode_t et4k_choose_default_mode(int is_ax_variant, uint32_t vram_bytes) {
    (void)is_ax_variant; (void)vram_bytes;
    return ET4000_MODE_640x480x8;
}

int et4k_detection_toggle_ok(void) { return 0; }
int et4k_detection_latch_ok(void) { return 0; }
int et4k_detection_signature_ok(void) { return 0; }
int et4k_detection_alias_limited(void) { return 0; }
uint32_t et4k_detection_alias_limit_bytes(void) { return 0; }

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
    uint8_t  vram_banks_limit;
    uint32_t vram_size_bytes;
    uint32_t hw_plane_pitch;
    uint8_t  hw_planes;
    uint8_t  shadow_bytes_per_pixel;
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
static uint8_t g_et4k_detected_banks = 0;
static uint32_t g_et4k_detected_vram = 0;
static int g_et4k_need_extended = 0;
static uint8_t g_et4k_last_attr16 = 0;
static uint8_t g_et4k_last_seq7 = 0;
static int g_et4k_last_vram_window_ok = 0;
static int g_et4k_palette_loaded = 0;
static int g_et4k_toggle_ok = 0;
static int g_et4k_latch_ok = 0;
static int g_et4k_signature_ok = 0;
static int g_et4k_alias_limited = 0;
static uint32_t g_et4k_alias_vram_limit = 0;
static int et4k_verify_vram_window(int require_extended_bits);
static void et4k_show_debug_status(const char* status_text);

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

static int et4k_debug_enabled(void) {
    return g_et4k_debug_trace || gpu_get_debug();
}

static void et4k_debug_print_detect(uint32_t vram_bytes) {
    if (!et4k_debug_enabled()) return;
    console_write("[et4k] Detected ");
    console_write(g_is_ax_variant ? "ET4000AX" : "ET4000");
    console_write(" (VRAM: ");
    if (vram_bytes) {
        console_write_dec(vram_bytes / 1024u);
        console_write(" KB");
    } else {
        console_write("unknown");
    }
    console_write(")");
    console_writeln("");
    console_write("[et4k] AX extended features available: ");
    console_writeln(g_is_ax_variant ? "YES" : "NO");
}

static void et4k_debug_print_alias_fallback(uint32_t fallback_bytes) {
    if (!et4k_debug_enabled()) return;
    console_write("[et4k] Bank 4 test failed -> falling back to ");
    if (fallback_bytes) {
        console_write_dec(fallback_bytes / 1024u);
        console_write(" KB limit");
    } else {
        console_write("256 KB limit");
    }
    console_writeln("");
}

static void et4k_console_hex8(uint8_t value) {
    static const char kHex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = kHex[(value >> 4) & 0x0F];
    buf[1] = kHex[value & 0x0F];
    buf[2] = '\0';
    console_write(buf);
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

static const uint8_t std_seq_640x480x4[5] = { 0x03,0x01,0x0F,0x00,0x02 };
static const uint8_t std_graph_640x480x4[9] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x0E,0x00,0xFF };
static const uint8_t std_attr_640x480x4[21] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x01,0x00,0x0F,0x00,0x00
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

static uint8_t et4k_probe_vram_banks(uint8_t saved_window, uint8_t saved_bank_reg);
static int et4k_bank_toggle_test(uint8_t saved_bank_reg) {
    uint8_t toggled = (uint8_t)((saved_bank_reg & 0xF0u) | ((saved_bank_reg ^ 0x01u) & 0x0Fu));
    outb(ET4K_BANK_PORT, toggled);
    et4k_io_delay();
    uint8_t readback = inb(ET4K_BANK_PORT);
    outb(ET4K_BANK_PORT, saved_bank_reg);
    et4k_io_delay();
    g_et4k_bank_read = (uint8_t)(saved_bank_reg & 0x0Fu);
    g_et4k_bank_write = (uint8_t)((saved_bank_reg >> 4) & 0x0Fu);
    return ((readback & 0x0Fu) == (toggled & 0x0Fu));
}

static int et4k_bank_latch_test(uint8_t saved_bank_reg) {
    uint8_t toggled = (uint8_t)(((saved_bank_reg ^ 0x10u) & 0xF0u) | (saved_bank_reg & 0x0Fu));
    outb(ET4K_BANK_PORT, toggled);
    et4k_io_delay();
    uint8_t readback = inb(ET4K_BANK_PORT);
    outb(ET4K_BANK_PORT, saved_bank_reg);
    et4k_io_delay();
    g_et4k_bank_read = (uint8_t)(saved_bank_reg & 0x0Fu);
    g_et4k_bank_write = (uint8_t)((saved_bank_reg >> 4) & 0x0Fu);
    return ((readback & 0xF0u) == (toggled & 0xF0u));
}

static int et4k_bank4_alias_detect(uint8_t saved_window, uint8_t saved_bank_reg) {
    const uint8_t bank_under_test = 4;
    et4k_set_window_raw(0x00);
    et4k_set_bank(0);
    volatile uint8_t* window = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;
    uint8_t orig0 = window[0];
    window[0] = 0xA5u;
    et4k_io_delay();

    et4k_set_bank(bank_under_test);
    uint8_t orig4 = window[0];
    window[0] = 0x5Au;
    et4k_io_delay();

    et4k_set_bank(0);
    uint8_t after0 = window[0];
    int alias = (after0 != 0xA5u);

    window[0] = orig0;
    et4k_io_delay();
    et4k_set_bank(bank_under_test);
    if (!alias) {
        window[0] = orig4;
    } else {
        window[0] = orig0;
    }
    et4k_io_delay();

    et4k_set_bank_rw((uint8_t)(saved_bank_reg & 0x0Fu), (uint8_t)((saved_bank_reg >> 4) & 0x0Fu));
    et4k_set_window_raw(saved_window);
    return alias;
}

static void et4k_debug_disable_nmi(void);

static int et4k_verify_vram_window(int require_extended_bits) {
    uint8_t saved_window = inb(ET4K_WINDOW_PORT);
    uint8_t saved_bank = inb(ET4K_BANK_PORT);
    uint8_t attr16 = et4k_atc_read(0x16);
    vga_attr_reenable_video();
    uint8_t seq7 = vga_seq_read(0x07);
    g_et4k_last_attr16 = attr16;
    g_et4k_last_seq7 = seq7;

    if (require_extended_bits) {
        if ((attr16 & 0x40u) == 0 || (seq7 & 0x10u) == 0) {
            if (g_et4k_debug_trace) {
                console_writeln("    [et4k] aperture bits not latched before VRAM test");
                console_write("      ATC[16]=0x");
                console_write_hex32(attr16);
                console_write(" SEQ[7]=0x");
                console_write_hex32(seq7);
                console_write("\n");
            }
            g_et4k_last_vram_window_ok = 0;
            gpu_debug_log("WARN", "et4k: aperture bits not latched");
            gpu_set_last_error("ERROR: aperture bits missing");
            return 0;
        }
    }

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

    if (observed != probe) {
        g_et4k_last_vram_window_ok = 0;
        gpu_debug_log("ERROR", "et4k: VRAM window test failed");
        gpu_set_last_error("ERROR: VRAM window test failed");
        return 0;
    }

    g_et4k_last_vram_window_ok = 1;
    gpu_debug_log("OK", "et4k: VRAM window verified");
    return 1;
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

static void et4k_debug_disable_nmi(void) {
#if CONFIG_VIDEO_ET4000_DEBUG_DISABLE_NMI
    static int nmi_disabled = 0;
    if (nmi_disabled) return;
    uint8_t selector = inb(0x70);
    outb(0x70, (uint8_t)(selector | 0x80u));
    nmi_disabled = 1;
    if (g_et4k_debug_trace) {
        console_writeln("    [et4k] NMIs disabled for debug (CONFIG_VIDEO_ET4000_DEBUG_DISABLE_NMI)");
    }
#endif
}

static uint8_t et4k_probe_vram_banks(uint8_t saved_window, uint8_t saved_bank_reg) {
    const uint8_t kMaxBanks = 16;
    volatile uint8_t* window = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;
    uint8_t originals[kMaxBanks];
    uint8_t patterns[kMaxBanks];

    uint8_t attr_saved = et4k_atc_read(0x16);
    vga_attr_reenable_video();
    uint8_t seq_saved = vga_seq_read(0x07);

    if ((attr_saved & 0x40u) == 0) {
        et4k_atc_write(0x16, (uint8_t)(attr_saved | 0x40u));
        vga_attr_reenable_video();
    }
    if ((seq_saved & 0x10u) == 0) {
        vga_seq_write(0x07, (uint8_t)(seq_saved | 0x10u));
    }

    et4k_set_window_raw(0x00);

    for (uint8_t bank = 0; bank < kMaxBanks; ++bank) {
        et4k_set_bank(bank);
        originals[bank] = window[0];
        patterns[bank] = (uint8_t)(0xA5u ^ (uint8_t)(bank * 0x13u));
    }

    uint8_t bank_count = 0;
    uint8_t last_written = 0;
    int wrote_any = 0;
    for (uint8_t bank = 0; bank < kMaxBanks; ++bank) {
        uint8_t pattern = patterns[bank];
        et4k_set_bank(bank);
        window[0] = pattern;
        et4k_io_delay();
        wrote_any = 1;
        last_written = bank;
        if (g_et4k_debug_trace) {
            console_write("    [et4k] probe bank ");
            console_write_hex32(bank);
            console_write(" write=");
            console_write_hex32(pattern);
        }
        if (window[0] != pattern) {
            if (g_et4k_debug_trace) {
                console_write(" read=");
                console_write_hex32(window[0]);
                console_writeln(" (mismatch)");
            }
            break;
        }
        int alias = 0;
        for (uint8_t prev = 0; prev < bank; ++prev) {
            et4k_set_bank(prev);
            if (window[0] != patterns[prev]) {
                alias = 1;
                if (g_et4k_debug_trace) {
                    console_write("    [et4k] bank ");
                    console_write_hex32(bank);
                    console_writeln(" aliases previous bank");
                }
                break;
            }
        }
        if (g_et4k_debug_trace && !alias) {
            console_write(" read=");
            console_write_hex32(window[0]);
            console_write("");
            console_writeln(" (ok)");
        }
        if (alias) {
            break;
        }
        bank_count = (uint8_t)(bank + 1u);
    }

    if (wrote_any) {
        for (uint8_t bank = 0; bank <= last_written && bank < kMaxBanks; ++bank) {
            et4k_set_bank(bank);
            window[0] = originals[bank];
            et4k_io_delay();
        }
    }

    et4k_set_bank_rw((uint8_t)(saved_bank_reg & 0x0Fu), (uint8_t)((saved_bank_reg >> 4) & 0x0Fu));
    et4k_set_window_raw(saved_window);

    if ((seq_saved & 0x10u) == 0) {
        vga_seq_write(0x07, seq_saved);
    }
    if ((attr_saved & 0x40u) == 0) {
        et4k_atc_write(0x16, attr_saved);
        vga_attr_reenable_video();
    }

    return bank_count;
}

static int et4k_configure_variant_registers(int require_extended_bits) {
    gpu_debug_log("INFO", "et4k: configuring variant registers");
    et4k_atc_video_disable();

    uint8_t attr16_before = et4k_atc_read(0x16);
    uint8_t attr16_target = attr16_before;
    if (g_is_ax_variant || require_extended_bits) {
        attr16_target |= 0x40u;
    }
    if (attr16_target != attr16_before) {
        et4k_atc_write(0x16, attr16_target);
    }
    vga_attr_reenable_video();
    uint8_t attr16_after = et4k_atc_read(0x16);
    vga_attr_reenable_video();
    g_et4k_last_attr16 = attr16_after;

    uint8_t seq7_before = vga_seq_read(0x07);
    uint8_t seq7_target = seq7_before;
    if (g_is_ax_variant || require_extended_bits) {
        seq7_target |= 0x10u;
    }
    if (seq7_target != seq7_before) {
        vga_seq_write(0x07, seq7_target);
    }
    uint8_t seq7_after = vga_seq_read(0x07);
    g_et4k_last_seq7 = seq7_after;

    if ((g_is_ax_variant || require_extended_bits)) {
        int attr_ok = (attr16_after & 0x40u) != 0;
        int seq_ok = (seq7_after & 0x10u) != 0;
        if (!attr_ok || !seq_ok) {
            if (g_et4k_debug_trace) {
                console_writeln("    [et4k] aperture bits failed to latch");
                console_write("      ATC[16]=0x");
                console_write_hex32(attr16_after);
                console_write(" SEQ[7]=0x");
                console_write_hex32(seq7_after);
                console_write("\n");
            }
            et4k_atc_video_enable();
            return 0;
        }
    }

    if (!g_is_ax_variant && g_et4k_debug_trace) {
        console_writeln("    [et4k] configured legacy Tseng aperture");
    }

    if (g_is_ax_variant) {
        et4k_trace("configuring ET4000AX extended bits");
        g_et4k_ax_engine_ready = et4kax_after_modeset_init();
        if (!g_et4k_ax_engine_ready) {
            et4k_disable_ax_engine("ax-init");
            if (!et4k_verify_vram_window(require_extended_bits)) {
                et4k_atc_video_enable();
                gpu_debug_log("ERROR", "et4k: aperture verification failed after AX init");
                return 0;
            }
            gpu_debug_log("WARN", "et4k: accelerator init failed, CPU fallback");
        }
    } else {
        vga_crtc_write(0x32, 0x00u);
        g_et4k_ax_engine_ready = 0;
    }

    et4k_set_window_raw(0x00);
    et4k_set_bank(0);
    et4k_atc_video_enable();
    if (!et4k_verify_vram_window(require_extended_bits)) {
        if (g_et4k_debug_trace) {
            console_writeln("    [et4k] VRAM window verification failed after configure");
        }
        gpu_debug_log("ERROR", "et4k: VRAM window verification failed after configure");
        return 0;
    }
    gpu_debug_log("OK", "et4k: aperture ready");
    return 1;
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
    if (g_et4k.vram_size_bytes > 0) {
        uint32_t max_offset = g_et4k.vram_size_bytes - 1u;
        if (start_offset > max_offset) {
            return;
        }
        if (end_offset > max_offset) {
            end_offset = max_offset;
        }
    }
    uint8_t first_bank = (uint8_t)(start_offset / ET4K_BANK_SIZE);
    uint8_t last_bank = (uint8_t)(end_offset / ET4K_BANK_SIZE);
    if (g_et4k.vram_banks_limit > 0) {
        uint8_t max_bank = (uint8_t)(g_et4k.vram_banks_limit - 1u);
        if (first_bank > max_bank) first_bank = max_bank;
        if (last_bank > max_bank) last_bank = max_bank;
    }
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
    uint32_t start = (uint32_t)y * g_et4k.pitch + (uint32_t)g_et4k.shadow_bytes_per_pixel * x;
    uint32_t end = (uint32_t)(y + height - 1u) * g_et4k.pitch + (uint32_t)g_et4k.shadow_bytes_per_pixel * (x + width - 1u);
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
    uint8_t pixel_value = (uint8_t)(g_et4k.bpp == 4 ? (color & 0x0Fu) : color);
    for (uint16_t row = 0; row < height; row++) {
        uint8_t* dst = g_et4k.shadow + ((uint32_t)(y + row) * pitch + x);
        et4k_memset8(dst, pixel_value, width);
    }
    uint32_t start = (uint32_t)y * pitch + (uint32_t)g_et4k.shadow_bytes_per_pixel * x;
    uint32_t end = (uint32_t)(y + height - 1u) * pitch + (uint32_t)g_et4k.shadow_bytes_per_pixel * (x + width - 1u);
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

static void et4k_sync_planar_4bpp(void) {
    if (!g_et4k.shadow) return;
    const uint32_t width = g_et4k.width;
    const uint32_t height = g_et4k.height;
    const uint32_t plane_pitch = g_et4k.hw_plane_pitch;
    const uint32_t shadow_pitch = g_et4k.pitch;
    if (!width || !height || !plane_pitch) return;

    et4k_set_window_raw(0x00);
    et4k_set_bank(0);
    volatile uint8_t* base = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;

    for (uint8_t plane = 0; plane < 4 && plane < g_et4k.hw_planes; ++plane) {
        vga_seq_write(0x02, (uint8_t)(1u << plane));
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* src_row = g_et4k.shadow + y * shadow_pitch;
            volatile uint8_t* dst_row = base + y * plane_pitch;
            for (uint32_t byte_idx = 0; byte_idx < plane_pitch; ++byte_idx) {
                uint8_t packed = 0;
                uint32_t pixel_base = byte_idx * 8u;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    uint32_t x = pixel_base + bit;
                    if (x >= width) break;
                    uint8_t pixel = src_row[x] & 0x0Fu;
                    uint8_t bit_val = (uint8_t)((pixel >> plane) & 0x01u);
                    packed |= (uint8_t)(bit_val << (7 - bit));
                }
                dst_row[byte_idx] = packed;
            }
        }
    }
    vga_seq_write(0x02, 0x0Fu);
}

static void et4k_sync(void* ctx) {
    (void)ctx;
    if (!g_et4k.shadow) return;
    if (g_et4k.bpp == 4) {
        if (!g_et4k.dirty_pending) return;
        et4k_sync_planar_4bpp();
        g_et4k.dirty_pending = 0;
        return;
    }
    if (!g_et4k.dirty_pending) return;
    et4k_set_window_raw(0x00);
    uint32_t range_start = g_et4k.dirty_start_offset;
    uint32_t range_end = g_et4k.dirty_end_offset;
    uint8_t last = g_et4k.dirty_last_bank;
    uint8_t bank_cap = last;
    if (g_et4k.vram_banks_limit > 0 && g_et4k.vram_banks_limit - 1u < last) {
        bank_cap = (uint8_t)(g_et4k.vram_banks_limit - 1u);
    }
    for (uint8_t bank = g_et4k.dirty_first_bank; bank <= last; bank++) {
        if (bank > bank_cap) {
            break;
        }
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

static void et4k_show_debug_status(const char* status_text) {
    if (!gpu_get_debug()) return;
    console_writeln("[et4k-status]");
    if (status_text && *status_text) {
        console_write("[ Status: ");
        console_write(status_text);
        console_writeln(" ]");
    }
    console_write("[ VRAM Aperture: ");
    console_write(g_et4k_last_vram_window_ok ? "OK" : "FAILED");
    console_writeln(" ]");
    console_write("[ Palette Loaded: ");
    console_write(g_et4k_palette_loaded ? "YES" : "NO");
    console_writeln(" ]");
    console_write("[ Framebuffer Pitch: ");
    console_write_dec(g_et4k.pitch);
    console_writeln(" ]");
    console_write("[ Detected Banks: ");
    console_write_dec(g_et4k_detected_banks);
    console_writeln(" ]");
    console_write("[ AX Engine: ");
    console_write(g_et4k_ax_engine_ready ? "READY" : "DISABLED");
    console_writeln(" ]");
    console_write("[ Last Error: ");
    console_write(gpu_get_last_error());
    console_writeln(" ]");
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
    g_et4k_detected_banks = 0;
    g_et4k_detected_vram = 0;
    g_et4k_need_extended = 0;
    g_et4k_last_attr16 = 0;
    g_et4k_last_seq7 = 0;
    g_et4k_ax_engine_ready = 0;
    g_et4k_last_vram_window_ok = 0;
    g_et4k_palette_loaded = 0;
    g_et4k_toggle_ok = 0;
    g_et4k_latch_ok = 0;
    g_et4k_signature_ok = 0;
    g_et4k_alias_limited = 0;
    g_et4k_alias_vram_limit = 0;
    gpu_debug_log("INFO", "et4k: starting detection");
    et4k_debug_disable_nmi();
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

    g_et4k_toggle_ok = et4k_bank_toggle_test(saved_bank_reg);
    g_et4k_latch_ok = et4k_bank_latch_test(saved_bank_reg);

    uint8_t unlocked_ext = et4k_unlock_value(saved_ext);
    if (unlocked_ext != saved_ext) {
        outb(ET4K_EXT_PORT, unlocked_ext);
        et4k_io_delay();
        g_ext_port_active = unlocked_ext;
        et4k_trace_hex("unlocked ext", unlocked_ext);
    } else {
        et4k_trace("extensions already unlocked");
    }
    et4k_io_delay();
    uint8_t ext_after_unlock = inb(ET4K_EXT_PORT);
    g_et4k_signature_ok = ((ext_after_unlock & 0x03u) == 0x03u);
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
        gpu_set_last_error("ERROR: Tseng signature mismatch");
        gpu_debug_log("ERROR", "et4k: signature check failed");
        return 0;
    }

    if (!g_et4k_toggle_ok) {
        if (g_ext_port_active != saved_ext) {
            outb(ET4K_EXT_PORT, saved_ext);
            et4k_io_delay();
            g_ext_port_active = saved_ext;
        }
        et4k_set_bank_rw(g_et4k_bank_read, g_et4k_bank_write);
        et4k_set_window_raw(saved_window);
        gpu_set_last_error("ERROR: Tseng bank toggle failed");
        gpu_debug_log("ERROR", "et4k: bank toggle test failed");
        return 0;
    }

    g_et4k_alias_limited = et4k_bank4_alias_detect(saved_window, saved_bank_reg);
    if (g_et4k_alias_limited) {
        g_et4k_alias_vram_limit = 4u * ET4K_BANK_SIZE;
        et4k_debug_print_alias_fallback(g_et4k_alias_vram_limit);
    }

    uint8_t bank_count = et4k_probe_vram_banks(saved_window, saved_bank_reg);
    if (g_et4k_alias_limited && bank_count > 4u) {
        bank_count = 4u;
    }
    if (bank_count == 0) {
        bank_count = 4;
        if (g_et4k_debug_trace) {
            console_writeln("    [et4k] VRAM probe inconclusive, assuming 256 KiB (4 banks)");
        }
    }
    uint32_t override_kb = CONFIG_VIDEO_ET4000_VRAM_OVERRIDE_KB;
    if (override_kb > 0) {
        uint8_t override_banks = (uint8_t)((override_kb * 1024u + ET4K_BANK_SIZE - 1u) / ET4K_BANK_SIZE);
        if (override_banks < 1) override_banks = 1;
        if (override_banks > 16u) override_banks = 16u;
        if (g_et4k_debug_trace) {
            console_write("    [et4k] VRAM override -> ");
            console_write_hex32(override_kb);
            console_writeln(" KiB");
        }
        bank_count = override_banks;
    }
    if (g_et4k_alias_limited && bank_count > 4u) {
        bank_count = 4u;
    }
    g_et4k_detected_banks = bank_count;
    g_et4k_detected_vram = (uint32_t)bank_count * ET4K_BANK_SIZE;
    if (g_et4k_alias_limited && g_et4k_alias_vram_limit) {
        g_et4k_detected_vram = g_et4k_alias_vram_limit;
    }
    g_et4k_need_extended = (bank_count > 4);
    if (gpu_get_debug()) {
        console_write("[DBG] et4k: VRAM banks detected = ");
        console_write_dec(bank_count);
        console_write(" (bytes=");
        console_write_hex32(g_et4k_detected_vram);
        console_writeln(")");
    }
    if (g_et4k_debug_trace) {
        console_write("    [et4k] detected banks=");
        console_write_hex32(bank_count);
        console_write(" (VRAM=");
        console_write_hex32(g_et4k_detected_vram);
        console_write(" bytes)\n");
    }

    et4k_bzero(out_info, sizeof(*out_info));
    int hw_ax = detect_et4000ax();
    if (hw_ax && !g_et4k_latch_ok) {
        hw_ax = 0;
        if (et4k_debug_enabled()) {
            console_writeln("[et4k] AX latch test failed -> disabling AX features");
        }
    }
    if (hw_ax && !g_et4k_signature_ok) {
        hw_ax = 0;
        if (et4k_debug_enabled()) {
            console_writeln("[et4k] Signature register unstable -> disabling AX features");
        }
    }
#if CONFIG_VIDEO_ET4000_AX_DISABLE
    if (g_et4k_debug_trace && hw_ax) {
        console_writeln("    [et4k-ax] AX detected but disabled via CONFIG_VIDEO_ET4000_AX_DISABLE");
    }
    g_is_ax_variant = 0;
#elif CONFIG_VIDEO_ET4000_AX_FORCE
    if (g_et4k_debug_trace && !hw_ax) {
        console_writeln("    [et4k-ax] forcing AX mode despite missing status bit");
    }
    g_is_ax_variant = 1;
#else
    g_is_ax_variant = hw_ax;
#endif
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
    out_info->framebuffer_size = g_et4k_detected_vram;
    g_et4k_saved_bank = saved_bank_reg;
    g_et4k_saved_window = saved_window;
    g_et4k_saved_state_valid = 1;
    if (g_ext_port_active != saved_ext) {
        outb(ET4K_EXT_PORT, saved_ext);
        et4k_io_delay();
        g_ext_port_active = saved_ext;
    }
    et4k_trace("detect end (success)");
    et4k_debug_print_detect(g_et4k_detected_vram);
    gpu_debug_log(g_is_ax_variant ? "OK" : "INFO", g_is_ax_variant ? "et4k: detected ET4000AX" : "et4k: detected ET4000");
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
    g_et4k.vram_banks_limit = 0;
    g_et4k.vram_size_bytes = 0;
    g_et4k.hw_plane_pitch = 0;
    g_et4k.hw_planes = 1;
    g_et4k.shadow_bytes_per_pixel = 1;
    g_et4k_last_vram_window_ok = 0;
    g_et4k_palette_loaded = 0;
}

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t shadow_pitch;
    uint32_t hw_plane_pitch;
    uint8_t  hw_planes;
    uint8_t  bpp;
    uint8_t  misc;
    const uint8_t* seq;
    const uint8_t* crtc;
    const uint8_t* graph;
    const uint8_t* attr;
} et4k_mode_desc_t;

static const et4k_mode_desc_t g_et4k_modes[] = {
    {
        640u, 480u, 640u, 640u, 1u, 8u, 0xE3,
        std_seq_640x480,
        std_crtc_640x480,
        std_graph_640x480,
        std_attr_640x480
    },
    {
        640u, 400u, 640u, 640u, 1u, 8u, 0xE3,
        std_seq_640x400,
        std_crtc_640x400,
        std_graph_640x400,
        std_attr_640x400
    },
    {
        640u, 480u, 640u, (640u + 7u) / 8u, 4u, 4u, 0xE3,
        std_seq_640x480x4,
        std_crtc_640x480,
        std_graph_640x480x4,
        std_attr_640x480x4
    }
};

static const et4k_mode_desc_t* et4k_lookup_mode(uint16_t width, uint16_t height, uint8_t bpp, et4000_mode_t* out_mode) {
    for (size_t i = 0; i < sizeof(g_et4k_modes) / sizeof(g_et4k_modes[0]); ++i) {
        const et4k_mode_desc_t* desc = &g_et4k_modes[i];
        if (desc->width == width && desc->height == height && desc->bpp == bpp) {
            if (out_mode) {
                *out_mode = (et4000_mode_t)i;
            }
            return desc;
        }
    }
    return NULL;
}

static uint32_t et4k_mode_required_bytes(const et4k_mode_desc_t* desc) {
    if (!desc) return 0;
    uint64_t pitch = desc->hw_plane_pitch;
    uint64_t planes = desc->hw_planes ? desc->hw_planes : 1u;
    uint64_t height = desc->height;
    uint64_t bytes = pitch * planes * height;
    if (bytes > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)bytes;
}

static int et4k_apply_mode_desc(gpu_info_t* gpu,
                                display_mode_info_t* out_mode,
                                const et4k_mode_desc_t* desc) {
    if (!gpu || !out_mode || !desc) return 0;

    if (gpu_get_debug()) {
        console_write("[et4k] mode request ");
        console_write_dec(desc->width);
        console_write("x");
        console_write_dec(desc->height);
        console_write("x");
        console_write_dec(desc->bpp);
        console_writeln("");
    }

    uint32_t required_bytes = et4k_mode_required_bytes(desc);
    uint32_t available_vram = g_et4k_detected_vram ? g_et4k_detected_vram : (uint32_t)(sizeof(g_et4k_shadow));
    if (required_bytes == 0 || required_bytes > available_vram) {
        if (et4k_debug_enabled()) {
            console_write("[et4k] mode requires ");
            console_write_dec(required_bytes / 1024u);
            console_write(" KB but only ");
            console_write_dec(available_vram / 1024u);
            console_writeln(" KB detected");
        }
        gpu_set_last_error("ERROR: mode exceeds detected VRAM capacity");
        gpu_debug_log("ERROR", "et4k: requested mode exceeds VRAM capacity");
        return 0;
    }

    et4k_enable_extensions();

    vga_program_standard_mode(desc->misc, desc->seq, desc->crtc, desc->graph, desc->attr);
    vga_pel_mask_write(0xFF);
    vga_dac_load_default_palette();
    g_et4k_palette_loaded = 1;

    int require_extended = (required_bytes > (4u * ET4K_BANK_SIZE)) || g_et4k_need_extended || g_is_ax_variant;
    if (!et4k_configure_variant_registers(require_extended)) {
        if (et4k_debug_enabled()) {
            console_writeln("[et4k] aperture setup failed during mode apply");
        }
        gpu_set_last_error("ERROR: aperture setup failed");
        gpu_debug_log("ERROR", "et4k: aperture setup failed");
        et4000_restore_text_mode();
        return 0;
    }

    if (desc->bpp == 4 && g_et4k_ax_engine_ready) {
        et4k_disable_ax_engine("4bpp-mode");
    }

    et4k_bzero(g_et4k_shadow, sizeof(g_et4k_shadow));

    g_et4k.width = desc->width;
    g_et4k.height = desc->height;
    g_et4k.pitch = desc->shadow_pitch;
    g_et4k.bpp = desc->bpp;
    g_et4k.shadow_bytes_per_pixel = 1;
    g_et4k.hw_plane_pitch = desc->hw_plane_pitch;
    g_et4k.hw_planes = desc->hw_planes;
    g_et4k.shadow = g_et4k_shadow;
    g_et4k.shadow_size = (uint32_t)g_et4k.pitch * g_et4k.height * g_et4k.shadow_bytes_per_pixel;
    g_et4k.vram_banks_limit = g_et4k_detected_banks ? g_et4k_detected_banks : 4;
    g_et4k.vram_size_bytes = available_vram;
    g_et4k.dirty_first_bank = 0;
    if (g_et4k.hw_planes > 1) {
        g_et4k.dirty_last_bank = 0;
    } else {
        uint8_t shadow_last_bank = (uint8_t)((g_et4k.shadow_size - 1u) / ET4K_BANK_SIZE);
        if (g_et4k.vram_banks_limit > 0 && shadow_last_bank >= g_et4k.vram_banks_limit) {
            shadow_last_bank = (uint8_t)(g_et4k.vram_banks_limit - 1u);
        }
        g_et4k.dirty_last_bank = shadow_last_bank;
    }
    g_et4k.dirty_pending = 1;

    if (g_is_ax_variant && g_et4k_ax_engine_ready) {
        fb_accel_register(&g_et4k_ops_ax, &g_et4k);
    } else {
        fb_accel_register(&g_et4k_ops_cpu, &g_et4k);
    }

    gpu->framebuffer_width = g_et4k.width;
    gpu->framebuffer_height = g_et4k.height;
    gpu->framebuffer_pitch = g_et4k.pitch;
    gpu->framebuffer_bpp = g_et4k.bpp;
    gpu->framebuffer_ptr = g_et4k.shadow;
    gpu->framebuffer_size = available_vram;

    out_mode->kind = DISPLAY_MODE_KIND_FRAMEBUFFER;
    out_mode->pixel_format = DISPLAY_PIXEL_FORMAT_PAL_256;
    out_mode->width = g_et4k.width;
    out_mode->height = g_et4k.height;
    out_mode->bpp = g_et4k.bpp;
    out_mode->pitch = g_et4k.pitch;
    out_mode->phys_base = gpu->framebuffer_base;
    out_mode->framebuffer = g_et4k.shadow;

    et4k_sync(&g_et4k);
    et4k_show_debug_status("framebuffer active");
    gpu_set_last_error("OK: Tseng framebuffer active");
    return 1;
}

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
    if (mode < ET4000_MODE_640x480x8 || mode >= ET4000_MODE_MAX) {
        gpu_set_last_error("ERROR: unsupported Tseng mode");
        return 0;
    }
    const et4k_mode_desc_t* desc = &g_et4k_modes[mode];
    return et4k_apply_mode_desc(gpu, out_mode, desc);
}

int et4k_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp) {
    et4000_mode_t mode_id;
    const et4k_mode_desc_t* desc = et4k_lookup_mode(width, height, bpp, &mode_id);
    if (!desc) {
        gpu_set_last_error("ERROR: unsupported Tseng mode");
        gpu_debug_log("ERROR", "et4k: unsupported mode request");
        return 0;
    }
    return et4k_apply_mode_desc(gpu, out_mode, desc);
}

et4000_mode_t et4k_choose_default_mode(int is_ax_variant, uint32_t vram_bytes) {
    if (is_ax_variant) {
        if (vram_bytes == 0 || vram_bytes >= (512u * 1024u)) {
            return ET4000_MODE_640x480x8;
        }
        return ET4000_MODE_640x400x8;
    }
    if (vram_bytes == 0) {
        return ET4000_MODE_640x400x8;
    }
    if (vram_bytes >= (256u * 1024u)) {
        return ET4000_MODE_640x400x8;
    }
    return ET4000_MODE_640x480x4;
}

void et4000_restore_text_mode(void) {
    fb_accel_reset();
    g_et4k_ax_engine_ready = 0;
    g_et4k_palette_loaded = 0;
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

void et4000_dump_bank(uint8_t bank, uint32_t offset, uint32_t length) {
#if !CONFIG_ARCH_X86
    (void)bank; (void)offset; (void)length;
    console_writeln("gpudump: not supported on this architecture");
#else
    if (length == 0) {
        length = 0x100u;
    }
    if (offset >= ET4K_BANK_SIZE) {
        console_writeln("gpudump: offset beyond bank window");
        return;
    }
    if (offset + length > ET4K_BANK_SIZE) {
        length = ET4K_BANK_SIZE - offset;
    }
    if (!g_et4k_detected_banks) {
        gpu_info_t info = {0};
        if (!et4000_detect(&info)) {
            console_writeln("gpudump: Tseng adapter not detected");
            return;
        }
    }
    if (g_et4k_detected_banks && bank >= g_et4k_detected_banks) {
        console_writeln("gpudump: bank exceeds detected VRAM");
        return;
    }

    uint8_t saved_window = inb(ET4K_WINDOW_PORT);
    uint8_t saved_bank_reg = inb(ET4K_BANK_PORT);
    uint8_t saved_read = (uint8_t)(saved_bank_reg & 0x0Fu);
    uint8_t saved_write = (uint8_t)((saved_bank_reg >> 4) & 0x0Fu);

    et4k_set_window_raw(0x00);
    et4k_set_bank(bank);

    volatile uint8_t* window = (volatile uint8_t*)(uintptr_t)ET4K_WINDOW_PHYS;
    const volatile uint8_t* base = window + offset;

    console_write("gpudump: bank ");
    console_write_dec(bank);
    console_write(", offset=0x");
    console_write_hex32(offset);
    console_write(", length=0x");
    console_write_hex32(length);
    console_writeln("");

    uint32_t absolute_base = (uint32_t)bank * ET4K_BANK_SIZE + offset;
    uint32_t processed = 0;
    while (processed < length) {
        uint32_t remaining = length - processed;
        uint32_t line_bytes = remaining > 16u ? 16u : remaining;
        uint8_t bytes[16];
        for (uint32_t idx = 0; idx < line_bytes; ++idx) {
            bytes[idx] = base[processed + idx];
        }

        console_write("  ");
        console_write_hex32(absolute_base + processed);
        console_write(": ");
        for (uint32_t idx = 0; idx < line_bytes; ++idx) {
            et4k_console_hex8(bytes[idx]);
            if (idx + 1u < line_bytes) {
                console_write(" ");
            }
        }
        for (uint32_t pad = line_bytes; pad < 16u; ++pad) {
            console_write("   ");
        }
        console_write(" |");
        for (uint32_t idx = 0; idx < line_bytes; ++idx) {
            uint8_t ch = bytes[idx];
            console_putc((ch >= 32u && ch < 127u) ? (char)ch : '.');
        }
        console_writeln("|");
        processed += line_bytes;
    }

    et4k_set_bank_rw(saved_read, saved_write);
    et4k_set_window_raw(saved_window);
#endif
}

int et4k_detection_toggle_ok(void) {
    return g_et4k_toggle_ok;
}

int et4k_detection_latch_ok(void) {
    return g_et4k_latch_ok;
}

int et4k_detection_signature_ok(void) {
    return g_et4k_signature_ok;
}

int et4k_detection_alias_limited(void) {
    return g_et4k_alias_limited;
}

uint32_t et4k_detection_alias_limit_bytes(void) {
    return g_et4k_alias_vram_limit;
}

#endif // !CONFIG_ARCH_X86
