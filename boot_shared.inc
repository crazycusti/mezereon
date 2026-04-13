; boot_shared.inc - Gemeinsame Definitionen für Boot-Code
%ifndef BOOT_SHARED_INC
%define BOOT_SHARED_INC

; Bootinfo Struktur Offsets
%define BOOTINFO_MAGIC      0x00    ; Magic Number (0xB007)
%define BOOTINFO_VERSION    0x02    ; Version der Bootinfo-Struktur
%define BOOTINFO_SIZE       0x04    ; Größe der Struktur
%define BOOTINFO_MEMSIZE    0x06    ; Verfügbarer Speicher in KB
%define BOOTINFO_BOOTDRIVE  0x08    ; Boot-Laufwerk
%define BOOTINFO_VBE_MODE       0x0A    ; bevorzugter 8bpp VBE Modus
%define BOOTINFO_VBE_PITCH      0x0C    ; Pitch für 8bpp Modus (Bytes/Zeile)
%define BOOTINFO_VBE_WIDTH      0x0E    ; Breite (Pixel) 8bpp
%define BOOTINFO_VBE_HEIGHT     0x10    ; Höhe (Pixel) 8bpp
%define BOOTINFO_VBE_BPP        0x12    ; Bits pro Pixel (8)
%define BOOTINFO_FB_ADDR        0x14    ; Framebuffer physische Adresse 8bpp

%define BOOTINFO_VBE4_MODE      0x18    ; bevorzugter 4bpp Modus (0 = keiner)
%define BOOTINFO_VBE4_PITCH     0x1A    ; Pitch 4bpp (Bytes/Zeile)
%define BOOTINFO_VBE4_WIDTH     0x1C    ; Breite 4bpp (Pixel)
%define BOOTINFO_VBE4_HEIGHT    0x1E    ; Höhe 4bpp (Pixel)
%define BOOTINFO_VBE4_BPP       0x20    ; Bits pro Pixel (4)
%define BOOTINFO_FB_ADDR4       0x22    ; Framebuffer physische Adresse 4bpp

%define BOOTINFO_VBE_MEM_BYTES  0x26    ; VRAM Größe in Bytes

; BIOS memory sizing fallbacks (stage2 scratch only; stage3 snapshots these before rebuilding boot_info_t)
%define BOOTINFO_BIOS_CONV_KB    0x2A    ; Conventional memory (KiB) from INT 12h
%define BOOTINFO_BIOS_EXT_KB     0x2C    ; Extended memory above 1MiB (KiB) from INT 15h (E801/88), dword
%define BOOTINFO_BIOS_MEM_FLAGS  0x30    ; Bitfield: 1=conv valid, 2=E801 valid, 4=88h valid

; Bootinfo Konstanten
%define BOOTINFO_MAGIC_VALUE 0xB007  ; Magic Number für Bootinfo
%define BOOTINFO_VERSION_1   0x0001  ; Version 1 der Bootinfo-Struktur

; Speicher-Layout (linear)
%define BOOTINFO_ADDR        0x0005000  ; Adresse der Bootinfo-Struktur
%define BOOTINFO_SEGMENT     (BOOTINFO_ADDR >> 4)
%define BOOTINFO_OFFSET      (BOOTINFO_ADDR & 0x0F)
%define KERNEL_LOAD_ADDR     0x0008000  ; Adresse an die der Kernel kopiert wird

; Anzahl Worte die beim Setup der Bootinfo gelöscht werden (sicher großzügig)
%ifndef BOOTINFO_CLEAR_WORDS
%define BOOTINFO_CLEAR_WORDS 512
%endif

; Übergabeparameter von Stage 2 an Stage 3
%define STAGE3_PARAM_BOOT_DRIVE      0x00
%define STAGE3_PARAM_STAGE3_LBA      0x04
%define STAGE3_PARAM_STAGE3_SECTORS  0x08
%define STAGE3_PARAM_FLAGS           0x0C
%define STAGE3_PARAM_GEOM_SPT        0x10
%define STAGE3_PARAM_GEOM_HEADS      0x12
%define STAGE3_PARAM_STAGE3_LOAD     0x14
%define STAGE3_PARAM_BOOTINFO_PTR    0x18
%define STAGE3_PARAM_KERNEL_LBA      0x1C
%define STAGE3_PARAM_KERNEL_SECTORS  0x20
%define STAGE3_PARAM_KERNEL_LOAD     0x24
%define STAGE3_PARAM_KERNEL_BUFFER   0x28
%define STAGE3_PARAM_E820_PTR        0x2C
%define STAGE3_PARAM_E820_COUNT      0x30
%define STAGE3_PARAM_SIZE            0x34

%define STAGE3_FLAG_A20_ENABLED      0x00000004
%define STAGE3_FLAG_KERNEL_PRELOADED 0x00000002
%define STAGE3_FLAG_SERIAL_BOOT      0x00000008

; Bootsektor-Hilfswerte (Stage 1)
%ifndef BOOT_DRIVE_OFFSET
%define BOOT_DRIVE_OFFSET 0x01FD
%endif

%ifndef BOOT_SIGNATURE
%define BOOT_SIGNATURE 0xAA55
%endif

%endif ; BOOT_SHARED_INC
