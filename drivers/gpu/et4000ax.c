#include "et4000ax.h"
#include "vga_hw.h"
#include "../../arch/x86/io.h"
#include <stddef.h>
#include <string.h>

#define AX_READY()   (inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY)
#define AX_RESET()   outb(ET4K_AX_ACCEL_CMD, 0x00)

// ET4000/AX Beschleunigungs-Status Bits
#define ET4K_AX_STATUS_BUSY    0x01
#define ET4K_AX_STATUS_READY   0x02

static void wait_for_fifo(void) {
    while (!AX_READY()) {
        outb(0x80, 0);
    }
}

static void wait_for_blitter(void) {
    while (inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_BUSY) {
        outb(0x80, 0);
    }
}

void et4000ax_bitblt(int sx, int sy, int dx, int dy, int width, int height, uint8_t rop) {
    wait_for_fifo();
    
    // Setze Quell- und Zielkoordinaten
    outw(ET4K_AX_SRC_X, sx);
    outw(ET4K_AX_SRC_Y, sy);
    outw(ET4K_AX_DEST_X, dx);
    outw(ET4K_AX_DEST_Y, dy);
    
    // Setze Breite und Höhe
    outw(ET4K_AX_WIDTH, width - 1);  // ET4000 verwendet 0-basierte Werte
    outw(ET4K_AX_HEIGHT, height - 1);
    
    // Starte BitBlt Operation
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_BITBLT | (rop << 4));
    
    wait_for_blitter();
}

void et4000ax_fill_rect(int x, int y, int width, int height, uint8_t color) {
    wait_for_fifo();
    
    // Setze Füllfarbe
    outb(ET4K_AX_FRGD_COLOR, color);
    
    // Setze Zielkoordinaten
    outw(ET4K_AX_DEST_X, x);
    outw(ET4K_AX_DEST_Y, y);
    
    // Setze Breite und Höhe
    outw(ET4K_AX_WIDTH, width - 1);
    outw(ET4K_AX_HEIGHT, height - 1);
    
    // Starte Rechteckfüllung
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_RECT_FILL);
    
    wait_for_blitter();
}

void et4000ax_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
    wait_for_fifo();
    
    // Setze Linienfarbe
    outb(ET4K_AX_FRGD_COLOR, color);
    
    // Setze Start- und Endpunkt
    outw(ET4K_AX_SRC_X, x1);
    outw(ET4K_AX_SRC_Y, y1);
    outw(ET4K_AX_DEST_X, x2);
    outw(ET4K_AX_DEST_Y, y2);
    
    // Starte Linienzeichnung
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_LINE);
    
    wait_for_blitter();
}

void et4000ax_pattern_fill(int x, int y, int width, int height, const uint8_t* pattern) {
    if (!pattern) return;
    
    wait_for_fifo();
    
    // Setze Muster (8x8 Pixel)
    for (int i = 0; i < 8; i++) {
        outb(ET4K_AX_PIXEL_MASK + i, pattern[i]);
    }
    
    // Setze Zielkoordinaten
    outw(ET4K_AX_DEST_X, x);
    outw(ET4K_AX_DEST_Y, y);
    
    // Setze Breite und Höhe
    outw(ET4K_AX_WIDTH, width - 1);
    outw(ET4K_AX_HEIGHT, height - 1);
    
    // Starte Musterfüllung
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_PAT_FILL);
    
    wait_for_blitter();
}

int et4kax_after_modeset_init(void) {
    uint8_t a16 = vga_attr_read(0x16);
    vga_attr_write(0x16, (uint8_t)(a16 | 0x40u));
    vga_attr_reenable_video();

    uint8_t s7 = vga_seq_read(0x07);
    vga_seq_write(0x07, (uint8_t)(s7 | 0x10u));

    vga_crtc_write(0x32, 0x28u);

    uint8_t c36 = vga_crtc_read(0x36);
    // Optional: enable IO/MEM wait states or 16-bit path; keep defaults for compatibility.
    vga_crtc_write(0x36, c36);

    vga_pel_mask_write(0xFF);

    AX_RESET();
    for (int t = 0; t < 100000; ++t) {
        if (AX_READY()) return 1;
        outb(0x80, 0);
    }
    return 0;
}
