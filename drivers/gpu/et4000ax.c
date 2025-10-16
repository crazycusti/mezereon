#include "et4000ax.h"
#include "vga_hw.h"
#include "../../arch/x86/io.h"
#include "../../console.h"
#include <stddef.h>
#include <stdint.h>

#define AX_READY()   (inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY)
#define AX_RESET()   outb(ET4K_AX_ACCEL_CMD, 0x00)
#define AX_TIMEOUT_ITERATIONS 150000

// ET4000/AX Beschleunigungs-Status Bits
#define ET4K_AX_STATUS_BUSY    0x01
#define ET4K_AX_STATUS_READY   0x02

static inline void et4kax_io_delay(void) {
    outb(0x80, 0);
}

static void et4kax_log(const char* msg) {
    if (!msg || !et4k_debug_trace_enabled()) return;
    console_write("    [et4k-ax] ");
    console_writeln(msg);
}

static void et4kax_log_hex(const char* label, uint8_t value) {
    if (!label || !et4k_debug_trace_enabled()) return;
    console_write("    [et4k-ax] ");
    console_write(label);
    console_write("=0x");
    console_write_hex32(value);
    console_write("\n");
}

static int g_et4kax_word_io = 0;

static int wait_for_fifo(void) {
    for (int i = 0; i < AX_TIMEOUT_ITERATIONS; ++i) {
        if (AX_READY()) {
            return 1;
        }
        et4kax_io_delay();
    }
    et4kax_log("timeout while waiting for FIFO ready");
    et4k_disable_ax_engine("fifo-timeout");
    return 0;
}

static int wait_for_blitter(void) {
    for (int i = 0; i < AX_TIMEOUT_ITERATIONS; ++i) {
        uint8_t status = inb(ET4K_AX_ACCEL_STATUS);
        if (!(status & ET4K_AX_STATUS_BUSY)) {
            return 1;
        }
        et4kax_io_delay();
    }
    et4kax_log("timeout while waiting for blitter idle");
    et4k_disable_ax_engine("blitter-timeout");
    return 0;
}

static void et4kax_writew(uint16_t port, uint16_t value) {
    if (g_et4kax_word_io) {
        outw(port, value);
    } else {
        outb(port, (uint8_t)(value & 0xFFu));
        et4kax_io_delay();
        outb((uint16_t)(port + 1u), (uint8_t)(value >> 8));
        et4kax_io_delay();
    }
}

static int et4kax_enable_register_window(void) {
    uint8_t attr = vga_attr_read(0x16);
    vga_attr_write(0x16, (uint8_t)(attr | 0x40u));
    vga_attr_reenable_video();
    uint8_t attr_check = vga_attr_read(0x16);
    vga_attr_reenable_video();
    if ((attr_check & 0x40u) == 0) {
        et4kax_log("ATC[16] bit6 did not latch; AX aperture closed");
        return 0;
    }

    uint8_t seq = vga_seq_read(0x07);
    vga_seq_write(0x07, (uint8_t)(seq | 0x10u));
    uint8_t seq_check = vga_seq_read(0x07);
    if ((seq_check & 0x10u) == 0) {
        et4kax_log("SEQ[7] bit4 did not latch; AX aperture closed");
        return 0;
    }

    et4kax_log_hex("atc16", attr_check);
    et4kax_log_hex("seq7", seq_check);
    return 1;
}

static void et4kax_program_waitstates(void) {
    uint8_t c36 = vga_crtc_read(0x36);
    uint8_t c36_wait = (uint8_t)((c36 & (uint8_t)~0xC0u) | 0x80u);
    if (c36_wait != c36) {
        vga_crtc_write(0x36, c36_wait);
    }
    et4kax_log_hex("crtc36", c36_wait);

    uint8_t c3e = vga_crtc_read(0x3E);
    uint8_t c3e_wait = (uint8_t)((c3e & (uint8_t)~0x03u) | 0x01u);
    if (c3e_wait != c3e) {
        vga_crtc_write(0x3E, c3e_wait);
    }
    et4kax_log_hex("crtc3e", c3e_wait);
    // TODO: expose CRTC[0x3C]/[0x3D] clock control when adding new video modes.
}

static int et4kax_detect_word_io(void) {
    uint8_t ctrl_orig = inb(0x3C3);
    uint8_t ctrl_test = (uint8_t)(ctrl_orig | 0x20u);
    outb(0x3C3, ctrl_test);
    et4kax_io_delay();
    uint8_t ctrl_verify = inb(0x3C3);
    outb(0x3C3, ctrl_orig);
    et4kax_io_delay();

    if (ctrl_verify & 0x20u) {
        const uint16_t port = ET4K_AX_WIDTH;
        uint16_t original = inw(port);
        const uint16_t pattern = 0x55AAu;
        outw(port, pattern);
        et4kax_io_delay();
        uint16_t observed = inw(port);
        int ok = (observed == pattern);
        outw(port, original);
        if (ok) {
            et4kax_log("ISA control bit5 asserted; using 16-bit AX writes");
            return 1;
        }
        et4kax_log("16-bit IO test failed; falling back to byte writes");
        return 0;
    }

    et4kax_log("ISA 16-bit control bit unavailable; using byte writes");
    return 0;
}

static int et4kax_reset_engine(void) {
    AX_RESET();
    for (int i = 0; i < AX_TIMEOUT_ITERATIONS; ++i) {
        if (AX_READY()) {
            return 1;
        }
        et4kax_io_delay();
    }
    et4kax_log("accelerator did not report ready after reset");
    return 0;
}

void et4000ax_bitblt(int sx, int sy, int dx, int dy, int width, int height, uint8_t rop) {
    if (width <= 0 || height <= 0) return;
    if (!wait_for_fifo()) return;

    et4kax_writew(ET4K_AX_SRC_X, (uint16_t)sx);
    et4kax_writew(ET4K_AX_SRC_Y, (uint16_t)sy);
    et4kax_writew(ET4K_AX_DEST_X, (uint16_t)dx);
    et4kax_writew(ET4K_AX_DEST_Y, (uint16_t)dy);

    et4kax_writew(ET4K_AX_WIDTH, (uint16_t)(width - 1));
    et4kax_writew(ET4K_AX_HEIGHT, (uint16_t)(height - 1));

    outb(ET4K_AX_ACCEL_CMD, (uint8_t)(ET4K_AX_CMD_BITBLT | ((rop & 0x0Fu) << 4)));

    wait_for_blitter();
}

void et4000ax_fill_rect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) return;
    if (!wait_for_fifo()) return;

    outb(ET4K_AX_FRGD_COLOR, color);

    et4kax_writew(ET4K_AX_DEST_X, (uint16_t)x);
    et4kax_writew(ET4K_AX_DEST_Y, (uint16_t)y);

    et4kax_writew(ET4K_AX_WIDTH, (uint16_t)(width - 1));
    et4kax_writew(ET4K_AX_HEIGHT, (uint16_t)(height - 1));

    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_RECT_FILL);
    wait_for_blitter();
}

void et4000ax_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
    if (!wait_for_fifo()) return;

    outb(ET4K_AX_FRGD_COLOR, color);

    et4kax_writew(ET4K_AX_SRC_X, (uint16_t)x1);
    et4kax_writew(ET4K_AX_SRC_Y, (uint16_t)y1);
    et4kax_writew(ET4K_AX_DEST_X, (uint16_t)x2);
    et4kax_writew(ET4K_AX_DEST_Y, (uint16_t)y2);

    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_LINE);
    wait_for_blitter();
}

void et4000ax_pattern_fill(int x, int y, int width, int height, const uint8_t* pattern) {
    if (!pattern) return;
    
    if (!wait_for_fifo()) return;

    for (int i = 0; i < 8; i++) {
        outb(ET4K_AX_PIXEL_MASK + i, pattern[i]);
    }
    
    et4kax_writew(ET4K_AX_DEST_X, (uint16_t)x);
    et4kax_writew(ET4K_AX_DEST_Y, (uint16_t)y);

    et4kax_writew(ET4K_AX_WIDTH, (uint16_t)(width - 1));
    et4kax_writew(ET4K_AX_HEIGHT, (uint16_t)(height - 1));

    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_PAT_FILL);
    wait_for_blitter();
}

int et4kax_after_modeset_init(void) {
    if (!et4kax_enable_register_window()) {
        return 0;
    }

    vga_crtc_write(0x32, 0x28u);
    if (et4k_debug_trace_enabled()) {
        uint8_t c32 = vga_crtc_read(0x32);
        et4kax_log_hex("crtc32", c32);
    }

    et4kax_program_waitstates();

    g_et4kax_word_io = et4kax_detect_word_io();

    vga_pel_mask_write(0xFF);

    if (!et4kax_reset_engine()) {
        return 0;
    }
    et4kax_log("accelerator ready");
    return 1;
}
