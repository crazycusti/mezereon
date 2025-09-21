NeeleFS v2 commands

- `neele mount [lba]` — mount a NeeleFS volume (default configured LBA)
- `neele ls [path]` — list directory contents; omit path for root
- `neele cat <name|/path>` — print a text file (non-printables map to `.`)
- `neele mkfs` — format a fresh NeeleFS v2 volume at the configured LBA
- `neele mkdir </path>` — create a directory on a mounted v2 volume
- `neele write </path> <text>` — write a short text payload (overwrites)
- `neele verify [verbose] [path]` — CRC check a file or directory tree; `verbose` prints per-file CRCs
- `pad </path>` — open the inline editor (Ctrl+S save, Ctrl+Q quit) on NeeleFS v2
