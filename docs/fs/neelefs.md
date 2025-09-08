# NeeleFS — Minimal FS (v1 read‑only, v2 write‑enabled)

Overview
- v1 (magic `NEELEFS1`): read‑only, flat table in the first sectors; used for simple boot images.
- v2 (magic `NEELEFS2`): write‑enabled, up to 16 MiB region, 512B blocks, bitmap allocation, directories with growth.
- Both variants are detected automatically by `neelefs_mount(lba)`; v2 enables write commands, v1 remains read‑only.

On‑Disk Layout (v2)
- Block size: 512B. Max region: 16 MiB → 32768 blocks.
- Superblock (block 0):
  - `magic[8]` = `NEELEFS2`, `version=2`, `block_size=512`, `total_blocks`.
  - `bitmap_start`: first bitmap block; `root_block`: first directory block.
  - `super_csum`: CRC32 over the 512B header with this field zero; verified on mount.
- Bitmap (blocks `bitmap_start .. bitmap_start + ceil(total/4096) - 1`): 1 bit per block.
- Directory blocks:
  - Header (16B): magic `'D2NE'` little‑endian, `next_block`, `entry_size=64`, `entries_per_blk`.
  - Entries (64B): `name[32]`, `type(1=file,2=dir)`, `first_block`, `size_bytes`, `csum(crc32 for files)`, `mtime(reserved)`.
  - Directories grow by appending a new block and linking via `next_block`.
- Files: stored contiguously (first‑fit allocation); checksum updated on `write`.

Shell Commands
- `neele mount [lba]`        → Mounts FS at `lba` (default: `CONFIG_NEELEFS_LBA`).
- `neele mkfs [force]`        → Formats v2 up to 16 MiB at `CONFIG_NEELEFS_LBA`.
  - Refuses to overwrite `NEELEFS1` (RO) or `NEELEFS2` unless `force` is supplied.
  - Probes available capacity (up to 16 MiB) and sizes bitmap + root accordingly.
- `neele ls [path]`           → Lists directory. With `path`, resolves directories first.
- `neele cat <name|/path>`    → v1: flat name; v2: supports `/path`.
- `neele mkdir </path>`       → v2 only; creates a directory (grows parent dir if needed).
- `neele write </path> <txt>` → v2 only; writes text into a new or existing file (contiguous allocation).
- `pad </path>`               → Simple nano‑like inline editor (v2). Ctrl+S=save, Ctrl+Q=quit. Max ~4 KiB.
- `neele verify [verbose] [</path>]` → Verify CRCs (single file or recursively). With `verbose`, prints CRCs for OK files too. If used, place `verbose` before the path.
- `autofs [show|rescan|mount <n>]` → Auto-detect ATA devices (PM/PS/SM/SS), detect NeeleFS at LBA 2048 or 0, attempt automount; show inventory or mount chosen index.

Pad Editor
- Usage: `pad </path>` (v2 only). Mount v2 first via `neele mount`.
- Load: preloads existing file contents if present; otherwise starts empty.
- Save: `Ctrl+S` writes the buffer to the file (`neelefs_write_text`).
- Quit: `Ctrl+Q` exits; warns if there are unsaved changes.
- Keys: printable ASCII and newline; Enter inserts `\n`; Backspace deletes one char; no cursor/navigation keys.
- Limit: buffer is ~4 KiB (see `shell.c`), sufficient for quick notes and README‑Style Files.
- Note: v1 (read‑only) images cannot be edited; use `neele mkfs` to create a v2 region.
- macOS Terminal tip: if `Ctrl+S` appears ignored or freezes the screen,
  disable software flow control in the shell before starting QEMU: `stty -ixon`.
  Re‑enable later with `stty ixon` if desired.
- CRC note: When a file's CRC fails verification during load, `pad` shows
  `pad: checksum mismatch` in the status bar and opens with an empty buffer.

Programming API (v2)
- `bool neelefs_mkfs_16mb(uint32_t lba)` / `bool neelefs_mkfs_16mb_force(uint32_t lba)`
- `bool neelefs_ls_path(const char* path)`
- `bool neelefs_mkdir(const char* path)`
- `bool neelefs_write_text(const char* path, const char* text)`
- `bool neelefs_read_text(const char* path, char* out, uint32_t out_max, uint32_t* out_len)`

Error Handling (typische Meldungen)
- Mount: `NeeleFS: bad magic` (kein FS); `NeeleFS2: bad super` (inkonsistent); `NeeleFS2: bad super (csum)`
- mkfs:
  - `NeeleFS2 already present; use 'neele mkfs force' to overwrite`
  - `NeeleFS1 volume detected (read-only); use 'neele mkfs force' to overwrite`
  - `mkfs: device too small (need >= 8KiB)`
  - `mkfs: not enough space for metadata`
  - `mkfs: write failed (device may be write-protected)`
  - `mkfs: bitmap write failed`
  - `mkfs: root init failed`
- Write ops on v1: `NeeleFS1 mounted (read-only); cannot write`
- Generic: `bad path`, `exists`, `not found`, `no space`
- File read: `checksum mismatch` (file CRC mismatch; read/cat aborts)

Quickstart
- Format & mount a v2 filesystem:
  - `neele mkfs`            (or `neele mkfs force` to overwrite legacy images)
  - `neele mount`           (uses `CONFIG_NEELEFS_LBA`)
  - `neele mkdir /docs`
  - `neele write /docs/readme "Hello NeeleFS v2"`
  - `neele ls /docs` and `neele cat /docs/readme`
  - `pad /docs/readme`      (inline editor; Ctrl+S=save, Ctrl+Q=quit)
  - `neele verify /docs`    (CRC check for files in /docs)

Notes & Limits
- Max region 16 MiB; block size fixed to 512B.
- Files are contiguous (no fragmentation handling yet); directories can grow block by block.
- Editor buffer limited (defaults to 4 KiB in shell); increase easily if needed.
- v1 images built with `tools/mkneelefs.py` are read‑only and remain readable via `neele mount/ls/cat`.
- Checksums: per‑file CRC32 is stored in each directory entry and recalculated on write. Reads verify CRC
  before returning/printing data. No separate checksum partition is needed; a dedicated checksum database would
  only be required for block‑level integrity or advanced features (journaling, dedup).
