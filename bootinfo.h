#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

// Architecture identifiers
#define BI_ARCH_X86   1
#define BI_ARCH_SPARC 2

// Minimal boot info structure, extensible per-arch
typedef struct boot_info {
    uint32_t arch;        // BI_ARCH_*
    uint32_t machine;     // optional machine id/platform
    const char* console;  // backend name, e.g. "vga_text"
    uint32_t flags;       // arch-specific flags
    void*    prom;        // firmware/prom vector (e.g. SPARC OBP)
} boot_info_t;

#endif // BOOTINFO_H

