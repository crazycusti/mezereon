; boot_shared.inc - Gemeinsame Definitionen für Stage 1 und Stage 2

; Boot-Info-Strukturen
%define BOOTINFO_SEG          0x0500
%define BOOTINFO_ADDR         0x5000
%define BOOTINFO_SIZE         1024
%define BOOTINFO_CLEAR_WORDS  (BOOTINFO_SIZE/2)

; Boot-Info-Offsets
%define BOOTINFO_ARCH_OFF     0x00
%define BOOTINFO_MACHINE_OFF  0x04
%define BOOTINFO_CONSOLE_OFF  0x08
%define BOOTINFO_FLAGS_OFF    0x0C
%define BOOTINFO_PROM_OFF     0x10
%define BOOTINFO_BOOTDEV_OFF  0x14
%define BOOTINFO_MEM_COUNT_OFF 0x18

; Memory-Map-Einträge
%define BOOTINFO_MEM_ENTRIES_BIAS  0x20
%define BOOTINFO_MEM_ENTRY_SIZE    20
%define BOOTINFO_MEM_MAX          32

; Architekturen
%define BI_ARCH_X86           0x86000000

; Flags
%define BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD  0x00000001

; SMAP-Signatur
%define SMAP_SIGNATURE        0x534D4150

; Boot Drive Location (stage1 -> stage2)
%define BOOT_DRIVE_OFFSET     0x1FD
%define BOOT_DRIVE_ADDR       0x7C00 + BOOT_DRIVE_OFFSET

; Kernel entry point
%define KERNEL_ENTRY_ADDR     0x8000
