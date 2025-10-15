# Bootloader-Stufen (Stage 1–3)

Dieser Leitfaden erklärt die drei Bootloader-Stufen der Mezereon-x86-Kette und richtet sich an alle, die verstehen wollen, wie BIOS-Start, Real‑Mode-Setup und der Übergang in den 32‑Bit-Bereich zusammenspielen. Für vertiefende Lern-Effekte enthalten die Abschnitte Querverweise auf relevante Symbole im Quelltext.

## Schneller Überblick
- **Stage 1** (`stage1.asm`): klassischer 512‑Byte-Bootsektor. Initialisiert Segmentregister, optional Debug-Ausgabe, lädt Stage 2 per BIOS INT 13h und springt im Real Mode weiter.
- **Stage 2** (`stage2.asm`): wohnt ab Segment `0x1000`. Wählt LBA/CHS, lädt Stage 3 und (falls konfiguriert) den Kernel, sammelt BIOS/E820/VBE-Informationen, aktiviert A20 sauber und wechselt in den Protected Mode.
- **Stage 3** (`stage3_entry.asm`, `stage3.c`): läuft bereits 32‑bittig. Validiert Übergabeparameter, lädt den Kernel bei Bedarf über ATA-PIO nach, baut `boot_info_t` auf und springt in die Kernel-Einstiegspunkt-Adresse.

## Stage 1 – `stage1.asm`
- **Ladeadresse:** BIOS lädt den Sektor nach `0x7C00`. Zu Beginn setzt Stage 1 `SS:SP` auf `0x0000:0x7C00` und spiegelt `DS` auf Null, damit Datenzugriffe erwartbar sind.
- **BIOS Parameter Block:** Die ersten 36 Bytes entsprechen einem FAT12-kompatiblen BPB. Damit akzeptieren Standard-BIOSse das Abbild auch von Disketten/USB-Sticks.
- **Disk-Reset & Laden von Stage 2:** Nach dem Sichern des Boot-Laufwerks (`dl → boot_drive`) ruft Stage 1 `INT 13h` AH=0x00 für einen Reset und AH=0x02, um `STAGE2_SECTORS` Sektoren ab `STAGE2_START_SECTOR` in das Segment `STAGE2_LOAD_SEGMENT` (Standard: `0x1000`) zu lesen.
- **Debug & Fehlverhalten:** Wird `ENABLE_STAGE1_DEBUG` gesetzt, zeichnet Stage 1 den Fortschritt mit Zeichen wie `1`, `.` oder `L` über BIOS-TTY (`int 10h, AH=0x0E`) in den Textmodus. Fehler melden sich mit `E` gefolgt vom BIOS-Statuscode.
- **A20-Handling:** Optional (`ENABLE_STAGE1_A20`) kann Stage 1 die schnelle Gate-A20-Methode (`port 0x92`) aktivieren, bevor der Sprung zu Stage 2 erfolgt (`jmp STAGE2_LOAD_SEGMENT:0000`).
- **Persistente Daten:** `boot_drive` liegt am Offset `0x1FD` (1 Byte) – direkt vor der Signatur `0xAA55`. Stage 2 liest dieses Byte unverändert weiter.

## Stage 2 – `stage2.asm`
- **Startumgebung:** Stage 2 beginnt ebenfalls im Real Mode, setzt Stack/Segmentregister neu und speichert das Boot-Laufwerk erneut.
- **BIOS-Erweiterungen erkennen:** `detect_disk_extensions` prüft, ob LBA-Zugriff möglich ist. Fällt der Check durch, erzwingt Stage 2 per Konstante (`STAGE2_FORCE_CHS`) den CHS-Codepfad.
- **Lade-Engine:** `load_sectors` kümmert sich um die gestückelten Transfers nach Linearadresse `STAGE3_LINEAR_ADDR` (Standard `0x40000`). `current_lba`, `remaining_sectors` und `buffer_linear` verwalten den Fortschritt.
- **Stage-3-Validierung:** Nach dem Einlesen prüft `check_stage3_signature`, dass die ersten sieben Bytes `mov ax,0x33534721; xor ax,ax` entsprechen – eine Absicherung, dass wirklich Stage 3 geladen wurde.
- **Bootinfo & Parameterblock:** `collect_e820` ruft `INT 15h, E820` bis zu 32 Einträge ab, während `populate_stage3_params` eine gepackte Struktur (`stage3_params`) am Ende von Stage 2 (ab `stage3_params`-Symbol) füllt. Wichtige Felder:
  - `boot_drive`, `flags` (Bit 0: LBA verwendet, Bit 1: Kernel bereits vorgeladen),
  - `stage3_load_linear`, `bootinfo_ptr`, `kernel_lba/sectors`, `kernel_buffer_linear`.
- **VBE/VGA-Infos:** Stage 2 fragt mit `INT 10h` AH=0x0F sowie VBE-Funktionen 0x4F00/0x4F01 nach aktuellen Video-Parametern und schreibt Pitch, Auflösung und Framebuffer-Adresse in den Bootinfo-Puffer (siehe `boot_shared.inc` Offsets).
- **Kernel-Vorladung:** `preload_kernel_if_needed` nutzt bei aktiviertem `ENABLE_BOOTINFO` bzw. HDD-Boot den BIOS-LBA-Modus, um den Kernel in einen Bounce-Puffer (`KERNEL_BUFFER_LINEAR`, Default `0x60000`) zu laden. Stage 3 kann dadurch sofort nach Protected-Mode-Start kopieren.
- **A20 & Protected Mode:** Stage 2 kombiniert `enable_a20_fast` (Port 0x92) und – falls `ENABLE_A20_KBC` aktiv – den klassischen Keyboard-Controller-Weg. `load_gdt` legt eine flache GDT (Code/Data) bei und `pm_stub` setzt die Segmentregister, bevor ein Far-Jump (`jmp CODE_SEL:STAGE2_LINEAR_ADDR+pm_stub`) in den 32‑Bit-Stub ausgeführt wird.

## Stage 3 – `stage3_entry.asm` & `stage3.c`
- **Entry-Stub:** `stage3_entry.asm` lädt eine eigene GDT (Code/Data, Basis = Laufzeitadresse) und springt mit `retf` in den 32‑Bit-Bereich. Der Stack zeigt danach auf `STAGE3_STACK_TOP` (default `0x9F000`).
- **Parameterübergabe:** Stage 2 übergibt in `ESI` den physikalischen Zeiger auf `stage3_params`, in `EDI` den Bootinfo-Puffer (`BOOTINFO_ADDR`). `stage3_main` prüft zuerst, ob die übergebenen Zeiger mit den erwarteten Werten übereinstimmen.
- **Kernel-Ladevorgang:** Ist `STAGE3_FLAG_KERNEL_PRELOADED` nicht gesetzt, lädt `stage3_load_kernel` den Kernel per ATA-28Bit-PIO (`ata_read_lba28`) in 4‑Sektoren-Blöcken in das Bounce-Buffer (oder direktes Ziel). Danach kopiert `stage3_memcpy` das Image an `kernel_load_linear`.
- **Bootinfo-Aufbau:** `stage3_build_bootinfo` initialisiert `boot_info_t`, übernimmt E820-Einträge sowie Video-Informationen und markiert das Boot-Gerät (HDD/Floppy). VBE-Daten werden aus dem von Stage 2 gefüllten Roh-Puffer (Offsets 0x0A–0x14) übernommen.
- **Debug-Hilfen:** Mit gesetztem `STAGE3_VERBOSE_DEBUG` schreibt Stage 3 sowohl in den VGA-Textspeicher als auch auf Port 0xE9 (`stage3_port_debug`). Praktisch zum Tracen in QEMU (`-debugcon stdio`).
- **Kernel-Start:** Abschließend interpretiert Stage 3 die Startadresse als Funktionspointer (`void (*kernel_entry)(void)`) und ruft sie. Eine Rückkehr würde `stage3_panic("return")` auslösen.

## Datenfluss zwischen den Stufen
- Stage 1 → Stage 2: BIOS-Laufwerk aus `DL` landet bei `boot_drive` (Offset `0x1FD`). Stage 2 liest dieses Byte direkt via `mov dl,[boot_drive]`.
- Stage 2 → Stage 3: `stage3_params` und der Bootinfo-Puffer liegen in zuvor festgelegten linearen Adressen. Register (`ESI`/`EDI`) sowie die GDT sind beim Protected-Mode-Übergang vorbereitet.
- Stage 3 → Kernel: Kernel wird auf seine Zieladresse kopiert, erhält einen initial gefüllten `boot_info_t` (Definition in `bootinfo.h`) und kann A20/GDT-Voraussetzungen übernehmen.

## Tipps zum Weiterlernen
- Aktiviere `STAGE1_VERBOSE_DEBUG` oder `STAGE2_VERBOSE_DEBUG` im `Makefile`, um die Fortschrittsausgaben zu verfolgen (`make STAGE1_VERBOSE_DEBUG=1 ...`).
- QEMU mit `-d int -debugcon stdio` zeigt Port-0xE9-Ausgaben und BIOS-INT-Aufrufe – hilfreich, um Stage 2/3 zu verfolgen.
- Nutze `make stage1.bin` bzw. `nasm -f bin stage1.asm` gezielt, um Änderungen an einzelnen Stufen schnell zu testen.
- Der Python-Check im `Makefile` validiert den Far-Jump in Stage 2 – bei Anpassungen am Protected-Mode-Pfad lohnt sich ein Blick auf dessen Konsolenausgabe.

