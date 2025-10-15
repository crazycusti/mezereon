#ifndef DRIVERS_GPU_ET4000AX_H
#define DRIVERS_GPU_ET4000AX_H

#include "gpu.h"
#include "et4000.h"

// ET4000/AX Beschleunigungs-Status Bits
#define ET4K_AX_STATUS_BUSY    0x01
#define ET4K_AX_STATUS_READY   0x02

// ET4000/AX spezifische Register
#define ET4K_AX_ACCEL_CMD      0x03B6  // Beschleunigungs-Befehlsregister
#define ET4K_AX_ACCEL_STATUS   0x03B6  // Status der Beschleunigungseinheit
#define ET4K_AX_FRGD_COLOR     0x03B8  // Vordergrundfarbe
#define ET4K_AX_BKGD_COLOR     0x03BA  // Hintergrundfarbe
#define ET4K_AX_PIXEL_MASK     0x03BC  // Pixel-Maske
#define ET4K_AX_SRC_X          0x03BE  // Quell-X-Position
#define ET4K_AX_SRC_Y          0x03C0  // Quell-Y-Position
#define ET4K_AX_DEST_X         0x03C2  // Ziel-X-Position
#define ET4K_AX_DEST_Y         0x03C4  // Ziel-Y-Position
#define ET4K_AX_WIDTH          0x03C6  // Breite für BitBlt
#define ET4K_AX_HEIGHT         0x03C8  // Höhe für BitBlt

// ET4000/AX Beschleunigungsbefehle
#define ET4K_AX_CMD_BITBLT     0x00    // BitBlt Operation
#define ET4K_AX_CMD_LINE       0x01    // Linienzeichnung
#define ET4K_AX_CMD_RECT_FILL  0x02    // Rechteckfüllung
#define ET4K_AX_CMD_PAT_FILL   0x03    // Musterfüllung

// ET4000/AX ROP Codes (Raster Operations)
#define ET4K_AX_ROP_COPY       0xCC    // Source kopieren
#define ET4K_AX_ROP_XOR       0x66    // XOR Operation
#define ET4K_AX_ROP_AND       0x88    // AND Operation
#define ET4K_AX_ROP_OR        0xEE    // OR Operation

// ET4000/AX spezifische Funktionen
void et4000ax_bitblt(int sx, int sy, int dx, int dy, int width, int height, uint8_t rop);
void et4000ax_fill_rect(int x, int y, int width, int height, uint8_t color);
void et4000ax_draw_line(int x1, int y1, int x2, int y2, uint8_t color);
void et4000ax_pattern_fill(int x, int y, int width, int height, const uint8_t* pattern);
int  et4kax_after_modeset_init(void);

#endif // DRIVERS_GPU_ET4000AX_H
