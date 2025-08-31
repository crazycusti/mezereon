#ifndef CONFIG_H
#define CONFIG_H

// Netzwerk-Parameter für NE2000, bitte anpassen Standardwerte sind 0x300 und IRQ 3 oder 5!
#define CONFIG_NE2000_IO   0x300
#define CONFIG_NE2000_IRQ  3
#define CONFIG_NE2000_IO_SIZE 32

// Optional: scan common ISA bases if configured base not found
#ifndef CONFIG_NE2000_SCAN
#define CONFIG_NE2000_SCAN 1
#endif

// Video params für Zeilenauflösung
#define CONFIG_VGA_WIDTH 80
#define CONFIG_VGA_HEIGHT 25

// Kernel version string
#ifndef CONFIG_KERNEL_VERSION
#define CONFIG_KERNEL_VERSION "0.01-prealpha"
#endif

// Video behavior
// 0 = preserve bootloader screen and append; 1 = clear screen on init
#ifndef CONFIG_VIDEO_CLEAR_ON_INIT
#define CONFIG_VIDEO_CLEAR_ON_INIT 0
#endif

#endif // CONFIG_H
