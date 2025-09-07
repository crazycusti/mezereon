# ATA (PIO) Support

Overview
- Implements ATA PIO for 28‑bit LBA reads and writes on the selected device.
- Default target is Primary Master (PM). You can switch devices at runtime.
- Designed for QEMU IDE (`-drive file=...,if=ide`). IRQs are not used; access is polling-based.

Shell Usage
- `ata`               → Prints whether the currently selected slot is an ATA disk.
- `ata scan`          → Scans standard slots: PM, PS, SM, SS. Prints detected type (none/ata/atapi).
- `ata use <0..3>`    → Select slot: 0=PM, 1=PS, 2=SM, 3=SS.
- `atadump [lba]`     → Hexdump up to 2KiB starting at given LBA (default 0). Navigation:
  - Down/Enter/Space = next line, PgDn = +16 lines, q = quit.

Functions (C API)
- `void ata_set_target(uint16_t io, uint16_t ctrl, bool slave)`
  - Selects the device (I/O base and control). Used by `ata use`.
- `ata_type_t ata_detect(void)`
  - Probes the selected target; returns `ATA_ATA`, `ATA_ATAPI`, or `ATA_NONE`.
- `bool ata_present(void)`
  - True if the selected target is a normal ATA disk.
- `bool ata_init(void)`
  - Initializes the selected ATA disk (IDENTIFY handshake and drains data).
- `bool ata_read_lba28(uint32_t lba, uint8_t sectors, void* buf)`
  - Reads 1..4 sectors (512B each) at `lba` into `buf`.
- `bool ata_write_lba28(uint32_t lba, uint8_t sectors, const void* buf)`
  - Writes 1..4 sectors from `buf` to disk at `lba`.
- `void ata_dump_lba(uint32_t lba, uint8_t sectors_max)`
  - Reads and prints up to 2 KiB in a formatted view; used by `atadump`.

QEMU Notes
- Typical run (headless smoke): `make test-x86-ne2k` (just a boot smoke test).
- Interactive (terminal/curses): `make run-x86-hdd`.
- Add a second IDE drive (manual) to host a NeeleFS image:
  - `qemu-system-i386 -drive file=disk.img,format=raw,if=ide -drive file=neele.img,format=raw,if=ide,index=1 -display curses`

Limits & Behavior
- 28‑bit LBA (up to 128 GiB) is supported for PIO transfers.
- PIO transfers are synchronous; large writes will busy-wait.
- No partition parsing; if you need a second volume, attach a second IDE drive and `ata use 1` or set the LBA of your FS header.

