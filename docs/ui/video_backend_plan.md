Video Backend Revamp — Initial Design Notes
===========================================

Goals
-----
- Provide a single front-end API (`console_*` / `video_*`) that can target different display backends (legacy VGA text, VESA/linear framebuffer, or future device-specific paths).
- Keep the abstraction cross-platform: x86 should support VESA/Cirrus; other arches (e.g. SPARC) must be able to hook in their own framebuffer or keep a stub without breaking builds.
- Preserve the existing text-mode experience as a fallback so that early bring-up / headless scenarios still work.
- Allow incremental porting: switch individual boards/targets to the new backend without touching unrelated code.

Current State
-------------
- `console_*` functions funnel straight into `video.c`, which writes to `0xB8000` VGA text memory.
- `console_backend_*` is selectable via `CONSOLE_BACKEND=vga|fb`, but both backends ultimately depend on the text-mode primitives; the "fb" variant is currently a stub.
- No PCI/VBE probing or framebuffer discovery logic exists; bootloader leaves the machine in VGA text mode.
- Architecture-specific surface area is minimal (mostly x86); other arches rely on dummy console output.

Design Direction
----------------
1. **Backend Interface**
   - Define a richer backend contract (`console_backend.h`) that covers:
     - Device capability query (text vs framebuffer, geometry, color depth)
     - Pixel/rect operations when framebuffer is available
     - Text rendering hooks (glyph cache) still exposed for text-only fallbacks
   - Introduce a common data structure (e.g., `struct mez_display_mode`) describing resolution, pitch, bpp, flags.

2. **Frontend Layering**
   - Keep `console_*` APIs as the canonical text/divider layer (so shell/logging code stays untouched).
   - Add a new lightweight framebuffer helper module (e.g., `fbcon.c`) responsible for drawing text/status onto an RGB surface when the backend advertises such support.
   - Route existing `video_*` helpers through the abstraction so they can select between legacy VGA text writes and framebuffer text rendering at runtime.

3. **Boot-Time Negotiation**
   - Extend the bootloader to detect VBE capability via INT 10h, select a mode (switchable via config), and pass the resulting LFB info to the kernel.
   - In absence of VBE (or on non-x86), fall back to VGA text or leave the choice to the platform-specific init code.
   - Architectures without BIOS (e.g. SPARC/OpenFirmware) can provide their own discovery hook later (OpenFirmware `display` package, etc.) without affecting x86 code.

4. **Configuration & Cross-Platform Handling**
   - Add configuration switches:
     - `CONFIG_VIDEO_MODE=auto|text|vesa` (compile/runtime policy)
     - Backend-specific options (`CONFIG_VBE_PREFERRED_WIDTH`, etc.)
   - Ensure non-x86 builds can force `text` or plug in a custom backend stub while we implement their framebuffer.

5. **Incremental Roll-out Plan**
   - Phase A: Refactor interface + provide stubbed capability checks (text only) to keep the kernel stable.
   - Phase B: Implement VBE detection + framebuffer init on x86, update console to use `fbcon` when available.
   - Phase C: Evaluate richer device integration (Cirrus acceleration, QEMU virtio-vga, etc.) once basic framebuffer path is proven.
   - Phase D: Port additional architectures/bootflows to the new abstraction.

Architecture Considerations
---------------------------
- **x86 BIOS/QEMU**: Primary target for VBE; needs careful assembly-side changes in the bootloader and new hand-off structures.
- **SPARC OpenBIOS**: No VGA text; still requires a workable fallback (current stub). Eventually can consume OpenFirmware framebuffer descriptors.
- **Future Targets (ARM, PowerPC)**: Likely rely on U-Boot/UEFI or device-tree-provided framebuffers; design the backend interface so it can be fed from firmware tables instead of BIOS calls.

Next Steps
----------
1. Implement the extended backend interface and supporting data structures (Phase A).
2. Update `console.c` / `video.c` to respect the capability flags while preserving legacy behavior.
3. Wire configuration defaults and ensure existing tests still pass.
4. Move on to VBE probing (Phase B) once the abstraction compiles on all platforms.


PCI & GPU Detection (Phase A.1)
-------------------------------
- Build a minimal PCI subsystem in-kernel:
  - x86 backend uses configuration mechanism #1 (ports `0xCF8/0xCFC`), other arches provide stubs or native hooks.
  - Enumerate buses/devices/functions; expose vendor/device IDs and BARs to drivers.
  - Provide helper macros for class codes to recognise VGA-compatible devices.
- Cirrus Logic GD5446 specifics to capture during detection:
  - Vendor 0x1013, Device 0x00B8 (`PCI_CLASS_DISPLAY_VGA`), BAR0 linear framebuffer, BAR1 MMIO (if present).
  - VRAM size via PCI ROM strap / configuration register (typically 2–4 MiB in QEMU, up to 8 MiB on real cards).
  - Feature bits: 2D BitBLT engine, hardware cursor, banked legacy VGA aperture, potential VBE BIOS pointer.
- Runtime reporting/logging:
  - On boot, print a summary for every VGA-class PCI device (vendor/device, bus:dev.fn, BARs, feature flags, VRAM, supported depths).
  - Expose a `gpuinfo` shell command that dumps the gathered information on demand.
- ISA considerations:
  - Keep legacy VGA text fallback active; detection via PCI misses ISA-only adapters.
  - Optional future work: probe VGA BIOS (INT 10h) or heuristic detection (probe memory at `0xA0000`) to list ISA adapters, but this requires vm86/real-mode shims and is deferred for now.
- Modularity:
  - Encapsulate Cirrus-specific initialisation in a dedicated driver module (`drivers/gpu/cirrus.c`).
  - Allow additional drivers to register (e.g., virtio-vga) without touching core PCI code.
- Testing path:
  - QEMU: run with `-device cirrus-vga` and `-device VGA` to validate detection, ensuring text backend remains untouched until framebuffer support is enabled.
  - Non-x86 builds should compile with stub PCI enumerator and continue to use the existing console path.

Status (2025-09-21)
-------------------
- PCI enumeration und Cirrus GD5446 probing sind aktiv; `gpuinfo`/`gpuinfo detail` liefern die Runtime-Infos.
- Automatischer Switch (`CONFIG_VIDEO_TARGET=auto|framebuffer`) aktiviert den Cirrus-LFB (640×480×8); die Konsole rendert jetzt im selben Codepfad sowohl VGA-Text als auch LFB (8×16-Glyphen, Regenbogen-Statusbar).
- `fbtest` prüft Palette/Renderer, Rückkehr in den Textmodus ist stabil.
