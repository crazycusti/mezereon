# Acumos AVGA2 (Cirrus Logic CL-GD5402) - Mezereon Hardware Documentation

## Hardware Overview
The Acumos AVGA2 is an ISA-based SVGA adapter using the Cirrus Logic CL-GD5402 chipset. It is typically found in early 386/486 systems.

### Specifications
- **VRAM:** Usually 256KB or 512KB.
- **Bus:** 16-bit ISA.
- **Interface:** Standard VGA registers + Cirrus extensions.

## Driver Implementation (`drivers/gpu/avga2.c`)

### Detection
The driver performs a two-stage detection:
1. **BIOS Signature:** Scans `0xC000:0x0000` for the string `"ACUMOS"`.
2. **Register Unlock:** Attempts to write `0x12` to Sequencer Register `0x06` (SR06). If the value can be read back, the chip is unlocked and identified.

### Memory Management (Shadow Buffer)
Due to the slow ISA bus and the need for bank switching (64KB window at `0xA0000`), the driver employs a **Shadow Buffer Strategy**:
- A linear 300KB+ buffer is allocated in high memory via `memory_alloc`.
- All drawing operations (`fb_accel_fill_rect`, etc.) write to this RAM buffer.
- The hardware is updated only during `fb_accel_sync()`.

### Bank Switching
Banking is controlled via Graphics Controller Register `0x09` (GR09):
- Bits 4-7: Bank selection (64KB increments).
- The `avga2_set_bank` function handles this during the sync process.

### Mode Support & Fallbacks
The driver automatically detects VRAM size via SR0F and chooses the best mode:
- **512KB+ VRAM:** Supports **640x480x8 (256 colors)**.
- **256KB VRAM:** Automatically falls back to **640x480x4 (16 colors, Planar)** or **320x200x8**.

## Text Mode Restoration
Returning from SVGA modes to standard 80x25 text mode on Cirrus hardware requires a precise sequence:
1. **Sequencer Reset:** SR00 = 0x01.
2. **Disable Extensions:** SR07 = 0x00 (Extended Mode off).
3. **Reset Banking:** GR09 = 0x00.
4. **I/O Mapping:** GR06 = 0x0E (Map to `0xB8000`, Graphics Mode = 0).
5. **Flip-Flop Reset:** Read `0x3DA` to reset Attribute Controller state.
6. **Font/Palette:** Standard VGA font and DAC palette reload.
7. **Sequencer Start:** SR00 = 0x03.

## Debugging
State can be dumped via the shell command `gpudump regs avga2`. Key registers:
- `SR06`: Unlock status (0x12).
- `SR07`: Extended mode status.
- `SR0F`: Memory configuration (VRAM size).
- `GR09`: Current bank.
