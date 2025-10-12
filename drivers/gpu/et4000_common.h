#ifndef ET4000_COMMON_H
#define ET4000_COMMON_H

#include "gpu.h"
#include "../../display.h"

// ET4000/AX Hardware Capabilities
#define GPU_CAP_ACCEL_BITBLT    (1 << 0)
#define GPU_CAP_ACCEL_FILL      (1 << 1)
#define GPU_CAP_ACCEL_LINE      (1 << 2)
#define GPU_CAP_ACCEL_PATTERN   (1 << 3)

// ET4000/AX Register Definitionen
#define ET4K_AX_ACCEL_CMD      0x03B6
#define ET4K_AX_ACCEL_STATUS   0x03B6
#define ET4K_AX_STATUS_READY   0x02

// GPU Info Struktur Erweiterung
typedef struct {
    uint32_t type;
    uint32_t caps;
    uint16_t max_width;
    uint16_t max_height;
    uint8_t  bpp;
    uint8_t  reserved[3];
} et4k_gpu_info_t;

// Externe Variablen
extern int g_is_ax_variant;

#endif // ET4000_COMMON_H