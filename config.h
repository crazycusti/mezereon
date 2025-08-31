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

// Keep VGA hardware cursor in sync with text output
#ifndef CONFIG_VIDEO_HW_CURSOR
#define CONFIG_VIDEO_HW_CURSOR 1
#endif

// ATA primary channel (QEMU default for -hda)
#ifndef CONFIG_ATA_PRIMARY_IO
#define CONFIG_ATA_PRIMARY_IO  0x1F0
#endif
#ifndef CONFIG_ATA_PRIMARY_CTRL
#define CONFIG_ATA_PRIMARY_CTRL 0x3F6
#endif

// NeeleFS default location (LBA). You can generate an image and map it
// to a second drive or place it at this LBA on the primary disk.
#ifndef CONFIG_NEELEFS_LBA
#define CONFIG_NEELEFS_LBA 2048
#endif

#endif // CONFIG_H
