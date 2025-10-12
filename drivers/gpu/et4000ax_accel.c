// ET4000AX Accelerator Implementation
#include "et4000ax.h"
#include "vga_hw.h"
#include "../../arch/x86/io.h"

// ET4000/AX Hardware-Spezifische Flags
#define AX_CTRL_ENABLE_ACCEL    0x20
#define AX_CTRL_8BPP_MODE       0x40
#define AX_CTRL_WAIT_READY      0x80

// Interner Status
static struct {
    int initialized;
    uint16_t max_width;
    uint16_t max_height;
    uint8_t  bpp;
    uint32_t features;
} ax_state = {0};

// Hardware-Initialisierung
static void ax_init_hw(void) {
    if (ax_state.initialized) return;
    
    // Reset Beschleuniger
    outb(ET4K_AX_ACCEL_CMD, 0x00);
    
    // Warte auf Ready
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0);
    
    // Aktiviere 8bpp Modus und Beschleuniger
    outb(ET4K_AX_ACCEL_CMD, AX_CTRL_ENABLE_ACCEL | AX_CTRL_8BPP_MODE);
    
    ax_state.initialized = 1;
    ax_state.max_width = 1024;
    ax_state.max_height = 768;
    ax_state.bpp = 8;
    ax_state.features = GPU_CAP_ACCEL_BITBLT | 
                       GPU_CAP_ACCEL_FILL |
                       GPU_CAP_ACCEL_LINE;
}

// BitBlt Operation
void ax_bitblt(int sx, int sy, int dx, int dy, int w, int h) {
    if (!ax_state.initialized) return;
    
    // Warte auf FIFO
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0);
    
    // Setze Parameter
    outw(ET4K_AX_SRC_X, sx);
    outw(ET4K_AX_SRC_Y, sy);
    outw(ET4K_AX_DEST_X, dx);
    outw(ET4K_AX_DEST_Y, dy);
    outw(ET4K_AX_WIDTH, w - 1);
    outw(ET4K_AX_HEIGHT, h - 1);
    
    // Starte BitBlt
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_BITBLT | ET4K_AX_ROP_COPY);
}

// Rechteck füllen
void ax_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (!ax_state.initialized) return;
    
    // Warte auf FIFO
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0);
    
    // Setze Farbe
    outb(ET4K_AX_FRGD_COLOR, color);
    
    // Setze Parameter
    outw(ET4K_AX_DEST_X, x);
    outw(ET4K_AX_DEST_Y, y);
    outw(ET4K_AX_WIDTH, w - 1);
    outw(ET4K_AX_HEIGHT, h - 1);
    
    // Starte Füllung
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_RECT_FILL);
}

// Linie zeichnen
void ax_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
    if (!ax_state.initialized) return;
    
    // Warte auf FIFO
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0);
    
    // Setze Farbe
    outb(ET4K_AX_FRGD_COLOR, color);
    
    // Setze Koordinaten
    outw(ET4K_AX_SRC_X, x1);
    outw(ET4K_AX_SRC_Y, y1);
    outw(ET4K_AX_DEST_X, x2);
    outw(ET4K_AX_DEST_Y, y2);
    
    // Starte Linienzeichnung
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_LINE);
}

// Pattern Fill mit 8x8 Muster
void ax_pattern_fill(int x, int y, int w, int h, const uint8_t* pattern) {
    if (!ax_state.initialized || !pattern) return;
    
    // Warte auf FIFO
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0);
    
    // Lade 8x8 Muster
    for (int i = 0; i < 8; i++) {
        outb(ET4K_AX_PIXEL_MASK + i, pattern[i]);
    }
    
    // Setze Parameter
    outw(ET4K_AX_DEST_X, x);
    outw(ET4K_AX_DEST_Y, y);
    outw(ET4K_AX_WIDTH, w - 1);
    outw(ET4K_AX_HEIGHT, h - 1);
    
    // Starte Pattern Fill
    outb(ET4K_AX_ACCEL_CMD, ET4K_AX_CMD_PAT_FILL);
}

// GPU Interface Funktionen
int ax_get_features(void) {
    return ax_state.initialized ? ax_state.features : 0;
}

void ax_get_max_resolution(uint16_t* width, uint16_t* height) {
    if (width) *width = ax_state.max_width;
    if (height) *height = ax_state.max_height;
}

int ax_is_initialized(void) {
    return ax_state.initialized;
}