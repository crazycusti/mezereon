Mezereon — Minimal x86 OS path + SPARC OBP stub

Quick start
- Show available targets: `make help`
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
- FS: “NeeleFS” simple RO image (`tools/mkneelefs.py`)
- SPARC: OpenFirmware OBP client program (prints banner, calls `kentry` SPARC branch)
 - Legacy SPARC docs moved to: `docs/legacy-sparc/`

Useful runtime commands (shell)
- help: list available shell commands.
- version: print kernel version string.
- clear: clear the screen and reset cursor.
- cpuinfo: show CPU vendor/family/model, features, brand.
- ata: report if selected ATA device is present (ATA disk).
- ata scan: scan PM/PS/SM/SS and print type per slot.
- ata use <0..3>: select device slot (0=PM,1=PS,2=SM,3=SS).
- atadump [lba]: hexdump up to 2KiB from LBA; Down/Enter/Space=next, PgDn=+16 lines, q=quit.
- neele mount [lba]: mount NeeleFS at LBA (default from CONFIG_NEELEFS_LBA).
- neele ls: list files in NeeleFS root.
- neele cat <name>: print file contents (non-printables as '.').
- netinfo: network summary (driver, MAC, promisc, IO base).
- netrxdump: verbose RX drain; press 'q' to quit.

Changelog
- Append quick notes: `make log MSG="keyword: short message"`
- File: `CHANGELOG`

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

- Enable NE2000 in QEMU (optional):
  - Quick: `make run-x86-hdd-ne2k`
  - Manual: `qemu-system-i386 -drive file=disk.img,format=raw,if=ide -serial stdio -device ne2k_isa,netdev=n0,io=0x300,irq=3 -netdev user,id=n0`
  - In shell: `netinfo` (MAC/IO/promisc), `netrxdump` (press 'q' to quit)
  - Ensure IO/IRQ match `CONFIG_NE2000_IO/IRQ` (defaults 0x300/3)

- SPARC netboot (OpenBIOS):
  - Build: `make sparc-boot`
  - Run: `make run-sparc-tftp`

CI / Automation
- GitHub Actions workflow included (`.github/workflows/ci.yml`). It:
  - installs deps on Ubuntu (`qemu-system-x86`, `nasm`, `gcc-multilib`, `binutils`)
  - runs `make clean && make -j2`
  - runs headless smoke test: `make test-x86-ne2k`
- Local quick test alias: `make test` → runs the same smoke test.
