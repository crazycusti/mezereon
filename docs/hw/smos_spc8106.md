# SMOS SPC8106 (Compaq Aero 4/33C)

## Identification
- **I/O Ports:** Auxiliary registers at `0x3DE` (Index) and `0x3DF` (Data).
- **Unlock Sequence:** Write `0x1A` to index `0x0E` or `0x1E`.
- **Revision Codes:**
    - `0x60`: F0A
    - `0x61`: F0B (Common in Aero, 256KB VRAM cap)

## Video Modes
### Standard VGA
- **Mode 13h (320x200x256):** Fully supported via `vga_hw.c`.
- **Mode 12h (640x480x16):** Supported via Planar-Sync (requires 4 planes).

### Extended Modes (Aero Sweet Spot)
- **Mode 100h (640x400x256):** 
    - Perfect fit for 256KB VRAM (256,000 bytes).
    - Implemented via Register-Hack: Disabling Double-Scan in CRTC `0x09` and adjusting vertical timings.
    - Requires 25MHz Pixel Clock.

## Driver Implementation Details
- **Shadow Buffer:** 300 KB, dynamically allocated above 1MB to prevent BSS collisions with BIOS areas.
- **Background Sync:** Planar-Sync for 4bpp guarded by `interrupts_save_disable()`.
- **Memory Mapping:** Always at `0xA0000` (Legacy VGA window). No PCI BAR remapping available.
- **Dynamic Console:** Text grid is recalculated based on height/16 and width/8.
