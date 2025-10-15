#ifndef ET4000_AX_DETECT_H
#define ET4000_AX_DETECT_H

#include "et4000_common.h"
#include "../../arch/x86/io.h"

int detect_et4000ax(void);

// ET4000AX Hardware-Initialisierung
static inline void init_et4000ax_hw(void) {
    if (!g_is_ax_variant) return;
    
    // Setze Standard-Parameter für den Beschleuniger
    outb(ET4K_AX_ACCEL_CMD, 0x00);   // Reset Beschleuniger
    
    // Warte auf Ready-Status
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0) {
        // Busy waiting
    }
}

// ET4000AX Beschleuniger-Funktionen aktivieren
static inline void enable_et4000ax_accel(et4k_gpu_info_t* gpu) {
    if (!gpu || !g_is_ax_variant) return;
    
    // Setze erweiterte Fähigkeiten
    gpu->caps |= GPU_CAP_ACCEL_BITBLT | 
                 GPU_CAP_ACCEL_FILL |
                 GPU_CAP_ACCEL_LINE |
                 GPU_CAP_ACCEL_PATTERN;
                 
    // Erweitere maximale Auflösung für AX
    gpu->max_width = 1024;
    gpu->max_height = 768;
    
    // Initialisiere Hardware
    init_et4000ax_hw();
}

#endif // ET4000_AX_DETECT_H
