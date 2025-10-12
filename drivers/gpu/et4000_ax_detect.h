#ifndef ET4000_AX_DETECT_H
#define ET4000_AX_DETECT_H

#include "et4000_common.h"
#include "../../arch/x86/io.h"

static int detect_et4000ax(void) {
    // Prüfe auf ET4000/AX spezifische Register
    outb(0x3C3, 0x20);
    uint8_t ax_id = inb(0x3C3);
    
    if ((ax_id & 0xF0) != 0x20) {
        return 0;  // Keine ET4000/AX
    }
    
    // Prüfe Beschleuniger-Register
    outb(ET4K_AX_ACCEL_CMD, 0x00);
    uint8_t status = inb(ET4K_AX_ACCEL_STATUS);
    
    return (status & ET4K_AX_STATUS_READY) != 0;
}

// ET4000AX Hardware-Initialisierung
static void init_et4000ax_hw(void) {
    if (!g_is_ax_variant) return;
    
    // Setze Standard-Parameter für den Beschleuniger
    outb(ET4K_AX_ACCEL_CMD, 0x00);   // Reset Beschleuniger
    
    // Warte auf Ready-Status
    while ((inb(ET4K_AX_ACCEL_STATUS) & ET4K_AX_STATUS_READY) == 0) {
        // Busy waiting
    }
}

// ET4000AX Beschleuniger-Funktionen aktivieren
static void enable_et4000ax_accel(et4k_gpu_info_t* gpu) {
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