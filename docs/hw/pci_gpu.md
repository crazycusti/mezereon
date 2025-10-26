PCI & GPU Detection
===================

Overview
--------
- Mezereon now scans PCI configuration space at boot (Mechanism #1 on x86) and tracks up to 32 devices.
- GPU drivers register against the sampled device table; the first implementation targets Cirrus Logic GD5446 (QEMU’s default VGA adapter).
- Discovery runs after the console comes up, so status messages land directly on the shell/log output and the results are also available via the `gpuinfo` command.
- Framebuffer-Aktivierung erfolgt seit der Textmodus-Forcierung ausschließlich manuell über `gpuprobe`, damit Debug-Ausgaben und Fehlersuche unverfälscht bleiben.

Cirrus Logic GD5446 notes
-------------------------
- Vendor/device IDs: `0x1013:0x00B8`.
- BAR0 exposes the linear framebuffer (size is calculated from the BAR mask at runtime); BAR1 exposes MMIO registers when present.
- Reported capabilities:
  - `linear-fb` — 32-bit accessible framebuffer aperture
  - `2d-accel` — BitBLT engine verfügbar (Kernel nutzt es für rechteckige Füllungen im Framebuffer)
  - `hw-cursor` — hardware cursor support is available
- The driver currently provides detection/logging only; programming the accelerator/LFB is planned for later phases.

Shell usage
-----------
- `gpuinfo` — dumps every supported adapter with bus/device/function, vendor/device IDs, BAR information, and capability flags.
- `gpuinfo detail` — includes Cirrus register snapshots (Sequencer, CRTC, Graphics, Attribute) to aid bring-up and debugging.
- `gpuprobe [scan|noscan] [auto|noauto] [status] [debug <on|off>] [activate <chip> <WxHxB>]` — führt den bekannten Diagnosepfad im Textmodus aus. Vor einer manuellen Aktivierung blendet das Tool die erkannten Framebuffer-Modi für den gewählten Chip ein (Auflösung × Farbtiefe) und verweist direkt auf die passende `activate`-Syntax.
- Boot-Log-Zeilen spiegeln die `gpuinfo`-Kurzfassung wider. Da der Bootvorgang im Textmodus verbleibt, muss ein Framebuffer nun bewusst via `gpuprobe activate ...` gewählt werden. `gpuinfo` listet weiterhin die bekannten Modi (gefiltert nach VRAM), während `gpuprobe` den Satz unmittelbar vor dem Umschalten zeigt.

Tseng ET4000 (ISA) notes
------------------------
- Detection toggles the Tseng-specific bank/segment registers (`0x3CB/0x3CD`). Activation is gated by `CONFIG_VIDEO_ENABLE_ET4000` (default: on).
- The driver exposes einen banked Framebuffer über ein Shadow-Buffer: per `CONFIG_VIDEO_ET4000_MODE` ist 640×480×4 aktuell der Standard; eine 640×400×8-Option bleibt für spätere AX-/Linear-Aperture-Unterstützung vorgesehen.
- `gpuinfo` marks the adapter as “legacy/ISA” (no PCI coordinates). Der Kernel verbleibt zunächst im Textmodus; `gpuprobe activate et4000 640x480x4` (oder `et4000ax`/`avga2`) aktiviert den gewünschten Modus. Vor der Umschaltung werden die verfügbaren Varianten erneut eingeblendet.
- Text-mode restore is supported (`gpu_restore_text_mode`), so shell commands like `fbtest`/`gpuinfo` leave the adapter in a sane state.

Architecture considerations
---------------------------
- Non-x86 builds compile with a stub enumerator (no devices detected) but keep the same interface so future platform code can plug in native PCI accessors.
- ISA-only adapters are still handled by the legacy VGA text backend — probing VGA BIOS or ISA cards is out-of-scope for the initial PCI work.

Next steps
----------
- Extend the GPU layer with additional drivers (e.g., QEMU `virtio-vga` or other PCI devices).
- Add optional Option-ROM / VBE probing once framebuffer mode switching lands.
- Wire framebuffer capabilities into the upcoming display backend so `gpuinfo` and the boot log reflect the active rendering path.
