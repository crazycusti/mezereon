#include "et4000ax.h"
#include "vga_hw.h"
#include "../../arch/x86/io.h"
#include <stddef.h>
#include <string.h>

// ET4000/AX Beschleunigungs-Status Bits
#define ET4K_AX_STATUS_BUSY    0x01
#define ET4K_AX_STATUS_READY   0x02

static void wait_for_fifo(void) {
    // Warte bis die Beschleunigungseinheit bereit ist
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0) {
        // Busy waiting
    }
}

static void wait_for_blitter(void) {
    // Warte bis der Blitter fertig ist
    while (inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_BUSY) {
        // Busy waiting
    }
}

int et4000ax_init(gpu_info_t* gpu) {
    if (!gpu) return 0;
    
    // Prüfe ob es wirklich eine ET4000/AX ist
    // TODO: Implementiere bessere Erkennung
    uint8_t id = inb(0x3C3);
    if ((id & 0xF0) != 0x20) return 0;
    
    // Setze die GPU-Info Struktur
    gpu->type = GPU_TYPE_ET4000AX;
    gpu->caps = GPU_CAP_ACCEL_BITBLT | 
                GPU_CAP_ACCEL_FILL |
                GPU_CAP_ACCEL_LINE;
                
    gpu->max_width = 1024;
    gpu->max_height = 768;
    gpu->max_bpp = 8;
    
    return 1;
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