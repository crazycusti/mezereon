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

// Sound Blaster 16 (SB16) legacy ISA defaults
#ifndef CONFIG_SB16_ENABLE
#define CONFIG_SB16_ENABLE 1
#endif

#ifndef CONFIG_SB16_IO
#define CONFIG_SB16_IO 0x220
#endif

#ifndef CONFIG_SB16_IRQ
#define CONFIG_SB16_IRQ 5
#endif

#ifndef CONFIG_SB16_DMA8
#define CONFIG_SB16_DMA8 1
#endif

#ifndef CONFIG_SB16_DMA16
#define CONFIG_SB16_DMA16 5
#endif

#ifndef CONFIG_SB16_SCAN
#define CONFIG_SB16_SCAN 1
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

// Wunschmodus für die Anzeige: 0=Text, 1=Automatik, 2=Framebuffer bevorzugen
#define CONFIG_VIDEO_TARGET_TEXT        0
#define CONFIG_VIDEO_TARGET_AUTO        1
#define CONFIG_VIDEO_TARGET_FRAMEBUFFER 2

#ifndef CONFIG_VIDEO_TARGET
#define CONFIG_VIDEO_TARGET CONFIG_VIDEO_TARGET_AUTO
#endif

#ifndef CONFIG_VIDEO_ENABLE_ET4000
#define CONFIG_VIDEO_ENABLE_ET4000 1
#endif

#ifndef CONFIG_VIDEO_ENABLE_CIRRUS
#define CONFIG_VIDEO_ENABLE_CIRRUS 1
#endif

#ifndef CONFIG_VIDEO_ENABLE_SMOS
#define CONFIG_VIDEO_ENABLE_SMOS 1
#endif

#ifndef CONFIG_GPU_DEBUG
#define CONFIG_GPU_DEBUG 1
#endif


#define CONFIG_VIDEO_ET4000_MODE_640x480x8 0
#define CONFIG_VIDEO_ET4000_MODE_640x400x8 1
#define CONFIG_VIDEO_ET4000_MODE_640x480x4 2

#ifndef CONFIG_VIDEO_ET4000_MODE
#define CONFIG_VIDEO_ET4000_MODE CONFIG_VIDEO_ET4000_MODE_640x480x4
#endif

#ifndef CONFIG_VIDEO_ET4000_VRAM_OVERRIDE_KB
#define CONFIG_VIDEO_ET4000_VRAM_OVERRIDE_KB 0
#endif

#ifndef CONFIG_VIDEO_ET4000_DEBUG_DISABLE_NMI
#define CONFIG_VIDEO_ET4000_DEBUG_DISABLE_NMI 0
#endif

// Kernel version string
#ifndef CONFIG_KERNEL_VERSION
#define CONFIG_KERNEL_VERSION "0.5.0-AeroAcumos"
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

// Paging policy (x86)
// AUTO: disable paging on very low-memory configs, enable otherwise (current default)
// NEVER: never enable paging
// ALWAYS: always try to enable paging (may still fail if page table allocation fails)
#define CONFIG_PAGING_POLICY_AUTO   0
#define CONFIG_PAGING_POLICY_NEVER  1
#define CONFIG_PAGING_POLICY_ALWAYS 2

#ifndef CONFIG_PAGING_POLICY
#define CONFIG_PAGING_POLICY CONFIG_PAGING_POLICY_AUTO
#endif

// AUTO threshold: if usable RAM (E820) is below this, skip enabling paging.
// 1024KiB matches "no paging under 1MiB" bring-up policy.
#ifndef CONFIG_PAGING_AUTO_MIN_USABLE_KB
#define CONFIG_PAGING_AUTO_MIN_USABLE_KB 1024
#endif

// System timer frequency (PIT IRQ0). Lower = fewer wakeups and lower host CPU load in QEMU.
#ifndef CONFIG_TIMER_HZ
#define CONFIG_TIMER_HZ 100
#endif

// Boot bring-up: enable IRQs only after IDT/PIC are stable.
#ifndef CONFIG_BOOT_ENABLE_INTERRUPTS
#define CONFIG_BOOT_ENABLE_INTERRUPTS 1
#endif

#ifndef CONFIG_DEBUG_SERIAL_PLUGIN
#define CONFIG_DEBUG_SERIAL_PLUGIN 1
#endif

#ifndef CONFIG_DEBUG_SERIAL_PORT
#define CONFIG_DEBUG_SERIAL_PORT 0x3F8
#endif

#ifndef CONFIG_DEBUG_SERIAL_BAUD
#define CONFIG_DEBUG_SERIAL_BAUD 19200
#endif

#ifndef CONFIG_DEBUG_SERIAL_HEARTBEAT_TICKS
#define CONFIG_DEBUG_SERIAL_HEARTBEAT_TICKS ((CONFIG_TIMER_HZ) ? (CONFIG_TIMER_HZ) : 100)
#endif

// Keyboard debug: print raw scancodes to console/serial
#ifndef CONFIG_KBD_DEBUG_DUMP
#define CONFIG_KBD_DEBUG_DUMP 1
#endif

// Keyboard probe loop during init (prints status + optional data)
#ifndef CONFIG_KBD_PROBE
#define CONFIG_KBD_PROBE 1
#endif

// Force scancode set 1 (disable if controller translation is enabled)
#ifndef CONFIG_KBD_FORCE_SET1
#define CONFIG_KBD_FORCE_SET1 0
#endif

// Accept AUX-flagged bytes as keyboard when AUX is disabled (QEMU quirk)
#ifndef CONFIG_KBD_ACCEPT_AUX
#define CONFIG_KBD_ACCEPT_AUX 1
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
