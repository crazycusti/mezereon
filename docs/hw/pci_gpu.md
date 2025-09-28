PCI & GPU Detection
===================

Overview
--------
- Mezereon now scans PCI configuration space at boot (Mechanism #1 on x86) and tracks up to 32 devices.
- GPU drivers register against the sampled device table; the first implementation targets Cirrus Logic GD5446 (QEMU’s default VGA adapter).
- Discovery runs after the console comes up, so status messages land directly on the shell/log output and the results are also available via the `gpuinfo` command.

Cirrus Logic GD5446 notes
-------------------------
- Vendor/device IDs: `0x1013:0x00B8`.
- BAR0 exposes the linear framebuffer (size is calculated from the BAR mask at runtime); BAR1 exposes MMIO registers when present.
- Reported capabilities:
  - `linear-fb` — 32-bit accessible framebuffer aperture
  - `2d-accel` — BitBLT engine is present (not yet programmed)
  - `hw-cursor` — hardware cursor support is available
- The driver currently provides detection/logging only; programming the accelerator/LFB is planned for later phases.

Shell usage
-----------
- `gpuinfo` — dumps every supported adapter with bus/device/function, vendor/device IDs, BAR information, and capability flags.
- `gpuinfo detail` — includes Cirrus register snapshots (Sequencer, CRTC, Graphics, Attribute) to aid bring-up and debugging.
- Boot-Log-Zeilen spiegeln die `gpuinfo`-Kurzfassung wider. Steht `CONFIG_VIDEO_TARGET` auf `auto` oder `framebuffer`, wechselt der Kernel bei Cirrus auf 640×480×8, zeichnet die Shell direkt auf den Framebuffer und färbt die Statusleiste mit einem dunklen Regenbogenhintergrund (`gfx: framebuffer`).

Architecture considerations
---------------------------
- Non-x86 builds compile with a stub enumerator (no devices detected) but keep the same interface so future platform code can plug in native PCI accessors.
- ISA-only adapters are still handled by the legacy VGA text backend — probing VGA BIOS or ISA cards is out-of-scope for the initial PCI work.

Next steps
----------
- Extend the GPU layer with additional drivers (e.g., QEMU `virtio-vga` or other PCI devices).
- Add optional Option-ROM / VBE probing once framebuffer mode switching lands.
- Wire framebuffer capabilities into the upcoming display backend so `gpuinfo` and the boot log reflect the active rendering path.
