# MMU / Memory in Mezereon (x86)

Stand: 2026-02-15

Dieses Dokument beschreibt, wie Mezereon beim Booten Speicher erkennt (BIOS/E820 und Fallbacks), wie daraus eine konsistente Speicherkarte entsteht, und wann/wie Paging aktiviert wird. Ziel ist, dass sowohl ein Mensch als auch der Agent schnell debuggen kann, warum eine bestimmte RAM-Groesse anders aussieht als erwartet.

## Begriffe (kurz)

- Physisch: Adresse auf dem RAM-Bus (was das BIOS/E820 beschreibt).
- Linear/virt: CPU-Sicht nach Segmentierung und ggf. Paging.
- Identity mapping: linear == physisch (virtuelle Adresse ist gleich physischer Adresse).
- E820: BIOS-Schnittstelle (INT 15h, EAX=0xE820) die eine Liste von Speicherbereichen liefert (usable/reserved/...).
- Conventional Memory: RAM unter 1MiB (klassisch ~640KiB).
- Extended Memory: RAM oberhalb 1MiB.
- HMA: High Memory Area, Fenster knapp ueber 1MiB: `0x00100000..0x0010FFF0` (64KiB-16).

## Datenfluss: vom BIOS bis zum Kernel

### 1) Stage2 (Real Mode): E820 + BIOS-Sizing

Stage2 sammelt Speicherinfos, solange wir noch im 16-bit Real Mode sind:

1. E820 Map (primaer)
   - INT 15h: `EAX=0xE820`, `EDX='SMAP'`, `ECX=24` (erst 24-Byte Entries, dann fallback 20-Byte).
   - Output Buffer ist `ES:DI` (Stage2 setzt `ES` ohne EAX zu clobbern).
   - DS wird vor dem BIOS-Call kurz auf 0 gesetzt, weil manche BIOS-Routinen das erwarten.
   - Stage2 speichert die Entries in einem internen 24-Byte Format (mit `attr`).
   - Debug optional via Port `0xE9` (`STAGE2_E820_DEBUG=1`, QEMU `-debugcon ... -global isa-debugcon.iobase=0xe9`).

2. BIOS Memory Sizing (Fallback fuer kaputtes/fehlendes E820)
   - INT 12h: liefert Conventional Memory in KiB.
   - INT 15h AX=E801: liefert Extended-Memory grob:
     - "unter 16MiB" in KiB plus "ueber 16MiB" in 64KiB-Bloecken.
     - Manche BIOSse liefern in AX/BX, andere in CX/DX; Stage2 akzeptiert beide.
   - INT 15h AH=88h (Fallback wenn E801 fehlschlaegt): Extended-Memory in KiB (oft gecappt).
   - Stage2 schreibt diese Werte in die Bootinfo-Scratch-Struktur (unter `BOOTINFO_ADDR`), damit Stage3 sie spaeter snapshottet.

Bootinfo-Scratch Offsets (siehe `boot_shared.inc`):

- `BOOTINFO_BIOS_CONV_KB` @ 0x2A (word)
- `BOOTINFO_BIOS_EXT_KB`  @ 0x2C (dword, KiB ueber 1MiB)
- `BOOTINFO_BIOS_MEM_FLAGS` @ 0x30 (word)
  - Bit 0: conv_kb valid
  - Bit 1: E801 valid
  - Bit 2: 88h valid

#### E820 typische Fallstricke (warum das hier so gebaut ist)

- BIOS ist pingelig bei Register/Segment-Zustaenden. Ein realer Bug in Mezereon war:
  - Stage2 setzte `EAX=0xE820`, hat dann aber spaeter `mov ax, cs` benutzt um `ES` zu setzen.
  - Das zerstoert das Low-Word von EAX und der BIOS-Call macht dann **nicht** E820, sondern irgendwas anderes (Resultat: `entry_count==0` / AH-Errorcodes).
  - Fix: `ES`/`DS` werden so gesetzt/restauriert, dass EAX nicht zerstoert wird (`push cs`/`pop es`, Restore nach `int 0x15` ebenfalls per `push cs`/`pop ds|es`).
- Manche BIOSse lesen `EDI` statt nur `DI` (deshalb `movzx edi, di` vor dem Call).
- Einige BIOSse erwarten fuer 24-Byte Entries ein nonzero "ext attr" dword (deshalb `mov dword [es:di+20], 1`).
- SeaBIOS/QEMU nutzt intern teils 32-bit Stack-Adressierung auch im Real Mode. Stage1/Stage2 setzen deshalb frueh `esp` so, dass die obere Haelfte garantiert 0 ist (sonst koennen BIOS-Calls sporadisch fehlschlagen).

### 2) Stage3 (32-bit): boot_info_t bauen

Stage3 baut die C-Struktur `boot_info_t` (siehe `bootinfo.h`) aus Stage2-Daten:

- Kopiert E820 Entries in `bootinfo->memory.entries[]`.
- Snapshottet zusaetzlich die BIOS-Sizing-Felder aus dem Stage2-Scratch (Offset 0x2A/0x2C/0x30) und schreibt sie in:
  - `bootinfo->bios_conventional_kb`
  - `bootinfo->bios_extended_kb`
  - `bootinfo->bios_mem_flags`

Wichtig: Stage3 leert/initialisiert `boot_info_t` neu, deshalb muss alles, was Stage2 in den Scratch schreibt und der Kernel spaeter braucht, vorher gesnapshottet und dann wieder gesetzt werden.

### 3) Kernel (kmain): Fallback, Normalisierung, Paging

Kernel-Sequenz (x86):

1. `kmain()` dump't die rohe E820-Karte (so wie sie vom Bootloader kam).
2. Falls die Karte fehlt/kaputt ist, wird eine synthetische Map gebaut (siehe unten).
3. `memory_init(bootinfo)` normalisiert die Karte (sortiert, entfernt Overlaps) und initialisiert den fruehen Allocator.
4. `paging_init(bootinfo)` entscheidet anhand Policy/Usable-RAM, ob Paging aktiviert wird.
5. Bootlog gibt `Paging: enabled|disabled` und danach `memory_log_summary()` aus.

## Kernel-Fallback: wann und wie

Implementiert in `main.c`:

- Fallback wird verwendet wenn:
  - `bootinfo == NULL`, oder
  - `bootinfo->memory.entry_count == 0`, oder
  - E820 meldet keine **usable** Region oberhalb 1MiB, aber BIOS `bios_extended_kb > 0` (E820 wirkt broken).

- Synthese der Map (ein Eintrag, bewusst simpel):
  1. Wenn `bios_extended_kb > 0`: `usable [1MiB .. 1MiB + ext_kb*1024)`
  2. Sonst wenn `bios_conventional_kb > 0`: `usable [0 .. conv_kb*1024)`
  3. Sonst last resort: `usable [1MiB .. 16MiB)`

Limitation: E801/88h liefern nur Groessen, keine "Holes". Die synthetische Map ist daher nur fuer Bring-up gedacht.

## memory_init(): Normalisierung + HMA + frueher Allocator

Implementiert in `memory.c`:

- Kopiert E820 Entries (bis `BOOTINFO_MEMORY_MAX_RANGES`), sortiert nach Base.
- Entfernt Overlaps via "consumed_end" (einfacher Sweep).
- Berechnet Summen:
  - `total_bytes` = Summe aller Regionen (nach Normalisierung)
  - `usable_bytes` = Summe aller usable Regionen
  - `highest_addr` = maximale Endadresse
- Ermittelt HMA, falls eine usable Region das Fenster `0x00100000..0x0010FFF0` schneidet.
- Initialisiert einen simplen "bump allocator":
  - bevorzugt HMA (wenn verfuegbar),
  - dann hoechste/nach Kernelende passende usable Region.

## Paging / MMU: GenV0 und GenV1 (aktueller Stand)

Mezereon trennt aktuell nicht in getrennte "Generationen" als API, aber das Verhalten entspricht:

- GenV0: **kein Paging**, flat/identity angenommen
- GenV1: **x86 32-bit Paging (4KiB Pages, non-PAE)**

### Policy (config.h)

- `CONFIG_PAGING_POLICY`:
  - `CONFIG_PAGING_POLICY_AUTO` (Default)
  - `CONFIG_PAGING_POLICY_NEVER`
  - `CONFIG_PAGING_POLICY_ALWAYS`
- `CONFIG_PAGING_AUTO_MIN_USABLE_KB` (Default 1024)
  - Unterhalb davon bleibt Paging aus (wichtig fuer Low-RAM/Old-BIOS Tests).

### Implementation (paging.c)

- Identity-Limit wird immer berechnet (auch wenn Paging spaeter deaktiviert bleibt):
  - mindestens `0x000C0000` (VGA/Low-Mem Baseline),
  - mindestens Kernel-Ende (aligned),
  - plus Endadressen aller **usable** Regionen,
  - optional Framebuffer-Window **nur wenn es in Low-Mem liegt** (high LFBs werden nicht identity-gemappt),
  - Cap: `PAGING_MAX_IDENTITY_TABLES` * 4MiB (der Rest der Page-Tables bleibt fuer ioremap uebrig).

- Page Directory + Tables werden **dynamisch** ueber `memory_alloc_aligned(4096,4096)` angelegt.
  - Vorteil: deutlich weniger `.bss` (wichtig fuer kleine RAM-Konfigurationen).
  - Wenn Allocation fehlschlaegt, bleibt Paging aus.

- Mapping:
  - 4KiB Identity Map bis zum Limit.
  - VGA-Window `0xA0000..0xBFFFF` wird als uncached gemappt (PWT|PCD), weil VGA/ISA das typischerweise braucht.
  - Device-Mappings (Framebuffer/MMIO) oberhalb des Identity-Limits werden per `paging_ioremap()` in ein virtuelles Fenster gemappt.

### Interaktion mit Display (framebuffer_reachable)

`display.c` prueft Framebuffer-Physadressen gegen `paging_identity_limit()` nur wenn Paging aktiv ist.
Wenn Paging deaktiviert ist, wird flat identity angenommen und der Framebuffer-Kandidat wird nicht allein wegen fehlendem Mapping abgelehnt.

Neu:
- Wenn Paging aktiv ist und `mode->framebuffer != (void*)mode->phys_base`, wird der Kandidat als "bereits gemappt" akzeptiert
  (ioremap-style, typischerweise virtuelle Adresse `0xE...`).

### Device Mapping (paging_ioremap)

- Zweck: High-LFBs (z.B. VBE LFB bei `0xFC000000`/`0xFD000000`) und PCI BARs im Kernel nutzbar machen, auch wenn Identity-Map
  klein bleibt (Low-RAM / konservativer Paging-Start).
- API: `paging_ioremap(phys, bytes, PAGING_IOREMAP_UNCACHED)` in `paging.c`/`paging.h`
- Virtuelles Fenster: aktuell `0xE0000000..0xFF000000` (page-aligned bump allocator).
- Standard: `PWT|PCD` fuer uncached (sicherer Default fuer MMIO/LFB; PAT/Write-Combining ist spaeter ein eigenes Thema).

## Debugging / Tests

### 1) Serielle Bootlogs

- Der Serial Debug Plugin loggt Bootinfo, inkl. `bios_mem: conv_kb/ext_kb/flags` (siehe `debug_serial.c`).
- Kernel loggt:
  - `E820 dump:` (raw)
  - optional `E820 fallback: ...`
  - `Paging: enabled|disabled`
  - `Memory:` Summary (`memory_log_summary()`).

### 2) Stage2 E820 Debug (Port 0xE9)

Beispiel (QEMU headless):

```bash
make disk.img
STAGE2_E820_DEBUG=1 make disk.img
timeout 10 qemu-system-i386 -no-reboot -m 16M \
  -drive file=disk.img,format=raw,if=ide -net none \
  -display none -monitor none -serial stdio \
  -debugcon file:logs/e9.log -global isa-debugcon.iobase=0xe9
```

### 3) RAM Sweep Script

Script: `tools/mem_sweep_x86.sh` (und `make mem-sweep-x86`).

```bash
TIMEOUT_SECS=6 make mem-sweep-x86
TIMEOUT_SECS=6 tools/mem_sweep_x86.sh 1M 1536K 2M 4M 64M
```

Spalten:
- `E820 count`: rohe Bootinfo entry_count
- `bios ext_kb`: BIOS Extended KiB (E801/88h)
- `fallback`: ob Kernel eine synthetische Map gebaut hat
- `total physical` / `usable`: Kernel-Summen nach Normalisierung
- `paging`: enabled/disabled
- `boot`: ob ein `mez>` Prompt gesehen wurde

Beobachtung (QEMU/SeaBIOS, aktueller Stand):

- `-m 1M` bootet (Paging disabled, nur 639KiB usable).
- `-m 1536K` bootet, aber knapp unter 1MiB usable -> Paging disabled (AUTO-Threshold).
- `-m 2M` und hoeher -> Paging enabled.
- `< 1MiB` (`-m 960K` und kleiner) liefert in dieser Umgebung kein Boot/keine serielle Ausgabe.
- Validierung: `-m 4M` lief in QEMU 60s stabil bis `mez>` (Heartbeats liefen durch, kein Reset/Triple-Fault). Beispiel-Logs: `logs/boot_4M_20260215_181724_serial.log` und `logs/boot_4M_20260215_181724_e9.log`.

## Roadmap: GenV2+ (noch nicht implementiert)

Geplante "Generationen" fuer Mapping-Sets:

- GenV2: 32-bit PAE (CR4.PAE, PDPT), fuer >4GiB oder feineres Layout.
- GenV3: 64-bit Long Mode (ueber separaten 64-bit Kernel/Entry).
- GenV4: UEFI Boot (Firmware Memory Map statt BIOS E820).

Die Selektionslogik sollte spaeter CPU-Features (CPUID: PAE/LM) und Firmware-Typ (BIOS/UEFI) kombinieren. Die BIOS-Sizing-Fallbacks bleiben trotzdem nuetzlich, weil alte BIOSse oft E820-Probleme haben.

## Relevante Dateien

- Bootloader:
  - `stage2.asm` (E820, BIOS sizing record)
  - `boot_shared.inc` (Bootinfo scratch offsets)
  - `stage3.c` (Snapshot -> `boot_info_t`)
  - `bootinfo.h`
- Kernel:
  - `main.c` (E820 dump + fallback decision, Paging log)
  - `memory.c` / `memory.h` (Normalisierung, HMA, Allocator)
  - `paging.c` / `paging.h` (Policy + Identity Paging)
  - `display.c` (framebuffer_reachable)
- Tools:
  - `tools/mem_sweep_x86.sh`
