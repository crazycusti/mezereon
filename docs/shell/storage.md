Storage helpers (ATA + autofs)

- `ata` — print whether an ATA device is present at the selected slot
- `ata scan` — probe the four IDE slots (PM/PS/SM/SS) and summarise what was found
- `ata use <0..3>` — switch the active device slot used by ATA commands
- `atadump [lba]` — interactive sector viewer (PgDn for +16 lines, `q` to exit)
- `autofs [show|rescan|mount <n>]` — rescan disks, show results, or mount a specific detected NeeleFS volume
