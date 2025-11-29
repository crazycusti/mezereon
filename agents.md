# Agents Notes — Mezereon

Goal: keep context small when adding features (drivers/apps) by pointing to stable extension points and checklists.

Repo compass
- Boot path: stage1/2/3 (16-bit/32-bit) → `entry32.asm` → `kentry.c` → `main.c`.
- Config surface: `config.h` (runtime defaults), Makefile vars (`CONFIG_*`, `VBE_*`, `CONSOLE_BACKEND`).
- Display stack: bootloader VBE handoff → `display_manager` → `video.c` text+FB renderer → `console_*`/shell → statusbar.
- Driver hubs: `drivers/pci.c`, `drivers/gpu/*`, `drivers/ata.c`, `drivers/ne2000.c`, `drivers/sb16.c`.
- APIs outward: `mezapi.[ch]` (in-kernel apps), shell commands in `shell.c`, docs under `docs/api/` + `docs/shell/`.

Quick boot/display facts
- Stage 2 picks an 8bpp VESA LFB if present (default 640x480). Tune via Makefile `VBE_PREF_WIDTH/HEIGHT/BPP`, `VBE_ENABLE_LFB`.
- Kernel applies the chosen target via `display_manager_apply_active_mode()`; `CONFIG_VIDEO_TARGET=text|auto|framebuffer`.
- GPU drivers call `display_manager_set_framebuffer_candidate` + `display_manager_apply_active_mode`; Cirrus enables accel via `cirrus_accel_enable`.
- Run graphics with `make run-x86-fb` (`QEMU_FB_DISPLAY=gtk|sdl`), text-only with `make run-x86-hdd`.

Driver/app onboarding checklist (keep changes scoped)
1) Pick the hook:
   - In-kernel app → new file under `apps/`, consume `mezapi.h`.
   - Device driver → `drivers/<domain>/`; expose a small `<name>.h` with init + ops and register from `main.c` or existing manager.
2) Config switches:
   - Add `CONFIG_FOO_*` to `config.h` (defaults) and pass through Makefile only if build-time.
   - Keep runtime toggles in shell (`shell.c`) or a tiny `*_debug` command.
3) Logging/UI:
   - User-facing text goes through `console_*`; status updates via `statusbar_*` (or MezAPI slot wrappers).
   - If you need FB drawing, prefer the existing 8bpp path; extend `video_switch_to_framebuffer` only when format-compatible.
4) MezAPI surface:
   - Only add to `mezapi.h`/`mezapi.c` when an app needs it; append new fields (preserve ABI layout) and bump `capabilities` bits if exposed.
   - Guard optional pointers (NULL allowed for new funcs).
5) Docs/changelog:
   - Summarize behavior in `docs/api/*.md` or a short `docs/hw/`/`docs/ui/` note.
   - Append a one-liner to `CHANGELOG` for any user-visible shift.

MezAPI map (v1, x86-32)
- Core: console write/clear, non-blocking input, ticks/timer hz/sleep, PC speaker + SB16 info, statusbar (legacy + slots).
- Video: `video_fb_get_info` (8bpp LFB), `video_fb_fill_rect` (uses accel when present), GPU info (feature level + caps).
- GPU feature levels: text-only, banked FB (ET4000/AVGA2), banked+accel (ET4000AX), linear FB, linear+accel (Cirrus).
- Cap bits: `MEZ_CAP_VIDEO_FB`, `MEZ_CAP_VIDEO_FB_ACCEL`, `MEZ_CAP_SOUND_SB16`, `MEZ_CAP_VIDEO_GPU_INFO`.

Extension points (where to plug new things)
- Console/display: add backends via `display_manager_set_*`; render policy lives in `video.c`.
- GPU: register new PCI adapters in `drivers/gpu/gpu.c`, mirror Cirrus pattern (detect → mode desc → `display_manager_*`).
- Net/storage: reuse `drivers/pci` or direct I/O; keep shell commands thin wrappers around driver APIs.
- Timer/IRQ: add per-IRQ init in `platform_*` and mask/unmask in `platform_irq_*`.

Pitfalls
- Framebuffer renderer is 8bpp-only; higher bpp needs format/planner updates before enabling.
- `-display curses` cannot show FB; use `run-x86-fb`.
- ET4000/AVGA2 remains debug-centric; auto-activation toggle `gpu_tseng_set_auto_enabled` / `CONFIG_VIDEO_ENABLE_ET4000`.

Useful commands while hacking
- `make`, `make run-x86-fb`, `make run-x86-hdd`, `make test-x86-ne2k`.
- Shell: `gpuinfo`, `gpuprobe activate ...`, `fbtest`, `cpuinfo`, `ip set ...`, `http start`.

Docs to skim
- `AI_HINTS.md`, `docs/api/mezapi.md`, `docs/api/graphics_fb.md`, `docs/ui/video_backend_plan.md`, `docs/hw/pci_gpu.md`, `docs/shell/*.md`.
