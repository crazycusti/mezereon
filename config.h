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

// NE2000 PIO width selection
// 1 = use 16-bit PIO (outw/inw) with DCR.WTS=1
// 0 = use 8-bit PIO (outb/inb) with DCR.WTS=0
#ifndef CONFIG_NE2000_PIO_16BIT
#define CONFIG_NE2000_PIO_16BIT 1
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

// Architecture selection (defaults to x86)
#ifndef CONFIG_ARCH_X86
#define CONFIG_ARCH_X86 1
#endif
#ifndef CONFIG_ARCH_SPARC
#define CONFIG_ARCH_SPARC 0
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

// Network RX debug printing from background service
#ifndef CONFIG_NET_RX_DEBUG
#define CONFIG_NET_RX_DEBUG 0
#endif

// Network receive mode helpers
// 0 = normal (accept unicast to station + broadcast)
// 1 = promiscuous (accept all frames, for debugging)
#ifndef CONFIG_NET_PROMISC
#define CONFIG_NET_PROMISC 0
#endif

#endif // CONFIG_H
