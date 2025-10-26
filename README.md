Mezereon — Minimal x86 OS path + SPARC OBP stub

Quick start
- Show available targets: `make help`
- Shell command cheat-sheets: `docs/shell/README.md`
- Serial debug plugin hardware notes: `docs/serial_debug_plugin.md`
- x86 (QEMU i386)
  - Build: `make`
  - Run (floppy, terminal/curses): `make run-x86-floppy`
  - Run (IDE disk, terminal/curses): `make run-x86-hdd`

- SPARC (QEMU SS-5, OpenBIOS)
  - Build: `make sparc-boot` (produces `arch/sparc/boot.{elf,aout,bin}`)
  - Netboot via TFTP (recommended): `make run-sparc-tftp`
  - Alternative: `make run-sparc-kernel` (direct `-kernel`, may vary by OpenBIOS build)
  - ISO boot helper: `make run-sparc-cdrom`
  
Networking (x86, NE2000)
- Quick run with NE2000 (terminal/curses): `make run-x86-hdd-ne2k` (usernet, io/irq from CONFIG)
- Manual QEMU example: `qemu-system-i386 -drive file=disk.img,format=raw,if=ide -serial stdio -device ne2k_isa,netdev=n0,io=0x300,irq=3 -netdev user,id=n0`

Dependencies
- Build tools: `gcc`, `nasm`, `ld`, `objcopy`
- x86: `qemu-system-i386`
- SPARC: `sparc-elf-gcc`/`sparc-unknown-elf-gcc`, `sparc-elf-objcopy`, `qemu-system-sparc`, `mkisofs`/`genisoimage`
Note: On some hosts you may need 32-bit development libraries to link with `-m32` (e.g. `gcc-multilib`).

Performance / Power (QEMU)
- Reduce guest timer interrupts: set `CONFIG_TIMER_HZ` (default 20) in `config.h`.
- At runtime: `timer hz 10` or `timer off` (mask IRQ0) to minimize wakeups while idling; `timer on` to re-enable.
- Enable hardware acceleration:
  - macOS: `QEMU_ACCEL=hvf make run-x86-hdd`
  - Linux: `QEMU_ACCEL=kvm make run-x86-hdd`
  - Windows: `QEMU_ACCEL=whpx make run-x86-hdd`

Storage Quickstart
- ATA:
  - Scan/select device: `ata scan` → `ata use <0..3>` (0=PM,1=PS,2=SM,3=SS)
  - Presence: `ata` (prints if ATA disk present)
  - Hexdump: `atadump [lba]` (view sectors, PgDn to scroll)
  - More details: `docs/storage/ata.md`

- NeeleFS:
  - Format v2 up to 16 MiB: `neele mkfs` (use `neele mkfs force` to overwrite legacy v1/v2)
  - Mount: `neele mount [lba]` (default `CONFIG_NEELEFS_LBA`)
  - List/cat: `neele ls [/path]`, `neele cat <name|/path>`
  - Write/dirs (v2 only): `neele mkdir </path>`, `neele write </path> <text>`
- Editor: `pad </path>` (Ctrl+S save, Ctrl+Q quit)
  - macOS Terminal: if `Ctrl+S` is swallowed or freezes output, run `stty -ixon` before `make run-*`.
  - More: `docs/fs/neelefs.md`

SPARC toolchain detection
- Native SPARC hosts: uses system `gcc/objcopy`.
- Other hosts: auto-detects cross compilers (`sparc-elf-gcc`, `sparc-unknown-elf-gcc`),
  and scans common crosstool-NG prefixes for `*/bin/sparc-unknown-elf-gcc` (or `sparc-elf-gcc`):
  `~/x-tools`, `/opt/x-tools`, `/usr/local/x-tools`. Also scans `$HOME/sparc32gcc/.build/**/bin/`.
- Additionally scans PATH generically for triplets matching `sparc-*-elf-gcc` and `sparc-*-elf-objcopy`.
- Prefer a specific toolchain directory by exporting `TOOLCHAIN_DIR` (expects `<dir>/bin/...` or `<dir>/...`):
  `TOOLCHAIN_DIR=/opt/x-tools/sparc-unknown-elf make sparc-boot`
- Optionally set an explicit prefix for modified toolchains:
  - `SPARC_PREFIX=sparc-mycustom-elf- make sparc-boot`
  - or use the common kernel-style `CROSS_COMPILE` prefix:
    `CROSS_COMPILE=sparc-custom-elf- make sparc-boot`
- Override via environment: `SPARC_CC=/path/to/sparc-*-gcc SPARC_OBJCOPY=/path/to/objcopy make sparc-boot`

What’s in here
- x86 path: BIOS bootloader (16-bit) → Protected Mode (`entry32.asm`) → C kernel (`kentry.c` → `main.c`)
- Devices: VGA text console, PIT timer, PIC remap, PS/2 keyboard, NE2000 (ISA) minimal NIC, ATA PIO LBA28
- FS: NeeleFS v1 (RO, `tools/mkneelefs.py`) and v2 (RW, up to 16 MiB)
- SPARC: OpenFirmware OBP client program (prints banner, calls `kentry` SPARC branch)
 - Legacy SPARC docs moved to: `docs/legacy-sparc/`

Useful runtime commands (shell)
- help: list available shell commands.
- version: print kernel version string.
- clear: clear the screen and reset cursor.
- cpuinfo: show CPU vendor/family/model, features, brand.
- ticks: print raw PIT tick counter (since boot).
- wakeups: print cpuidle HLT wakeups (since init).
- idle [n]: execute HLT once (or n times) to test CPU idle handling.
- Shell highlights (full cheat-sheets live in `docs/shell/`):
  - Storage: `ata`, `autofs`, `atadump` (see `docs/shell/storage.md`).
  - NeeleFS v2: `neele mount/ls/cat/mkfs/verify`, `pad` editor (`docs/shell/neelefs.md`).
  - Network: `netinfo`, `netrxdump` (set `CONFIG_NET_RX_DEBUG=1` for verbose driver logs), `ip`, `http` (`docs/shell/network.md`, `docs/net/http.md`).
  - GPU/Pci: `gpuinfo [detail]` prüft PCI-Grafikgeräte; `detail` zeigt Register-Dumps. `gpuinfo` listet verfügbare Auflösungen/Farbtiefen (basierend auf erkanntem VRAM) und nennt auch erkannte ISA-Adapter wie Tseng ET4000 oder Acumos AVGA2. `gpuprobe` nutzt die registrierten Adapter, zeigt vor einer manuellen Aktivierung alle passenden Framebuffer-Modi (Auflösung × Farbtiefe) samt kopierfertigem `gpuprobe activate <chip> ...`-Befehl an und lädt Tseng-Legacy-Scans nur bei Bedarf (`scan` oder manuelle ET4000/AVGA2-Anforderung). Weitere Details: `docs/shell/gpu.md`.
  - Grafikmodus: `CONFIG_VIDEO_TARGET=text|auto|framebuffer` (Default `auto`). Der Bootvorgang bleibt aus Debug-Gründen im Textmodus; Framebuffer lassen sich später mit `gpuprobe` aktivieren. Die Statusleiste zeigt weiterhin den aktiven Modus (`gfx: framebuffer`/`text`).
  - Power/system: `version`, `idle`, `timer`, `ticks`, `wakeups` (`docs/shell/system.md`, `docs/shell/power.md`).
  - Apps: `app ls`, `app run <name|/path>` for MezAPI-linked helpers such as `keymusic` or `rotcube`; see `docs/shell/apps.md`, `docs/api/mezapi.md`, `docs/api/graphics_fb.md`.
  - Audio quick test: `beep [freq] [ms]` (PC speaker ping).

Changelog
- Append quick notes: `make log MSG="keyword: short message"`
- File: `CHANGELOG`
- Workflow details: `docs/workflow/changelog.md`

Cleanup
- `make clean` removes build artifacts and images
- `make distclean` additionally prunes untracked build products (keeps VCS/metadata)

Legacy helpers
- Several ad-hoc SPARC helper scripts have been deprecated in favor of Makefile targets
  and now print a short message. Preferred flows:
  - Build: `make sparc-boot`
  - Run (netboot): `make run-sparc-tftp`
  - Run (direct kernel): `make run-sparc-kernel`

Examples
- Inspect disk blocks:
  - Build + run on x86 HDD: `make && make run-x86-hdd`
  - In shell: `atadump 0` (Down/Enter/Space=next, PgDn=+16, q=quit)

- Create and read NeeleFS:
  - Build a small image: `mkdir -p demo && echo "Hello Mez" > demo/hello.txt && python3 tools/mkneelefs.py demo neele.img`
  - Start QEMU with second IDE drive (manual):
    - `qemu-system-i386 -drive file=disk.img,format=raw,if=ide -drive file=neele.img,format=raw,if=ide,index=1 -net none -serial stdio`
  - In shell: `neele mount 2048` → `neele ls` → `neele cat hello.txt`
  - Update an existing disk image in-place: `python3 tools/neelefs2_put.py disk.img ./host.txt /www/host.txt --lba 2048`

- Enable NE2000 in QEMU (optional):
  - Quick: `make run-x86-hdd-ne2k`
  - Manual: `qemu-system-i386 -drive file=disk.img,format=raw,if=ide -serial stdio -device ne2k_isa,netdev=n0,io=0x300,irq=3 -netdev user,id=n0`
  - In shell: `netinfo` (MAC/IO/promisc), `netrxdump` (press 'q' to quit)
  - Ensure IO/IRQ match `CONFIG_NE2000_IO/IRQ` (defaults 0x300/3)
  - Host port forwarding (usernet): forward host port 8080 → guest 80 for HTTP
    - `HTTP_HOST_PORT=8080 make run-x86-hdd-ne2k`
    - Host: `curl http://127.0.0.1:8080/`
    - Use `0.0.0.0` binding if you want to accept from LAN: run QEMU manually with `-netdev user,id=n0,hostfwd=tcp:0.0.0.0:8080-:80`

- Serve a simple web page:
  - In shell:
    - `neele mkfs && neele mount`
    - `neele mkdir /www`
    - `neele write /www/index.html "<html><body><h1>Hello</h1></body></html>"`
    - `ip set 10.0.2.15 255.255.255.0 10.0.2.2`
    - `http file /www/index && http start 80` (default path is already `/www/index`)
  - From host: `curl http://10.0.2.15:80/`
  - More: `docs/net/http.md`

- SPARC netboot (OpenBIOS):
  - Build: `make sparc-boot`
  - Run: `make run-sparc-tftp` (preferred; closely mimics SS-5 firmware expectations)
  - Run (direct kernel): `make run-sparc-kernel` (image is linked via `arch/sparc/link.ld` with a single PT_LOAD; OpenBIOS builds still vary in behaviour)

CI / Automation
- GitHub Actions workflow included (`.github/workflows/ci.yml`). It:
  - installs deps on Ubuntu (`qemu-system-x86`, `nasm`, `gcc-multilib`, `binutils`)
  - runs `make clean && make -j2`
  - runs headless smoke test: `make test-x86-ne2k`
- Local quick test alias: `make test` → runs the same smoke test.
