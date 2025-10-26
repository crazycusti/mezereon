# Memory-Management in Mezereon

Dieser Kernel verarbeitet die vom Bootloader übergebene BIOS-E820-Liste und
erstellt daraus einen konsistenten Speicherplan. Die wichtigsten Schritte sind:

1. **E820 normalisieren** – `memory_init()` kopiert die E820-Einträge, sortiert
   sie nach Basisadresse und entfernt Überlappungen. Dabei werden nur Bereiche
   mit `length > 0` übernommen. Die aufbereiteten Einträge stehen anschließend in
   `memory_region_t`-Strukturen zur Verfügung.【F:memory.c†L11-L117】
2. **Summen berechnen** – Während der Normalisierung werden Gesamt-, Nutz- und
   Höchstadressen ermittelt. Die Ausgabe von `memory_log_summary()` zeigt alle
   Bereiche samt Hex-/Größenangaben und prüft damit direkt, ob die „usable“-Summe
   zur Karte passt.【F:memory.c†L216-L279】
3. **Allocator initialisieren** – Nach dem Aufbau des Plans werden zwei Cursor
   gesetzt: einer für den High-Memory-Pool und einer für den High Memory Area
   (HMA). Beide starten hinter dem Kernel (`&_end`, 16-Byte-ausgerichtet), damit
   Kernel und Bootstrukturen nicht überschrieben werden.【F:memory.c†L119-L171】
4. **HMA verwenden** – Befindet sich ein `usable`-Bereich im Fenster
   `0x100000–0x10FFF0`, wird er als HMA erkannt und bevorzugt für frühe
   Allokationen genutzt. Erst wenn der HMA erschöpft ist, greift der Allocator auf
   regulären RAM zurück.【F:memory.c†L69-L87】【F:memory.c†L171-L215】

## HMA – High Memory Area

Der HMA umfasst 64 KiB direkt oberhalb der 1 MiB-Grenze. Historisch wurde er von
Real-Mode-Programmen genutzt, sobald A20 aktiv war. Im Mezereon-Kernel steht er
ebenfalls als regulärer, vom BIOS als „usable“ markierter Speicher zur Verfügung.
Der Allocator prüft deshalb beim Initialisieren jeden `usable`-Bereich auf HMA-
Überlappung und stellt ihn als separaten Pool bereit. Allokationen laufen dann so
ab:

1. Versuche eine Zuweisung im HMA (`g_mem.hma_cursor`). Passt Größe und
   Ausrichtung, wird der Cursor verschoben und der Pointer zurückgegeben.
2. Falls der HMA nicht reicht, iteriert der Kernel über alle `usable`-Bereiche.
   Für jeden Bereich wird die aktuelle Cursorposition ausgerichtet und geprüft,
   ob der Block hineinpasst. Anschließend wird der globale Cursor fortgesetzt.

Auf diese Weise werden klassische Low-Memory-Puffer (z. B. BIOS- oder ISA-DMA
Strukturen) automatisch in den HMA gelegt, während größere Strukturen im „normalen“
RAM landen.【F:memory.c†L228-L279】

## Lernhinweise

- Die Ausgabe direkt nach dem Boot (`memory_log_summary()`) zeigt die komplette
  E820-Karte, die berechneten Summen und den HMA-Status. So lässt sich leicht
  überprüfen, ob der Bootloader korrekte Bereiche markiert und ob sich daraus
  stimmige Gesamtgrößen ergeben.【F:memory.c†L216-L279】
- Möchte man weitere Pools (z. B. für DMA <16 MiB) ergänzen, bietet sich der
  vorhandene Mechanismus mit separaten Cursoren an – einfach einen Bereich
  markieren, Cursor initialisieren und vor den High-Memory-Schritt stellen.
- `memory_alloc_aligned()` arbeitet rein linear („bump allocator“) und eignet
  sich für frühe Boot-Phasen. Für einen echten Heap könnte man später freie
  Listen hinzufügen; die vorberechneten Bereiche bilden die Grundlage dafür.【F:memory.c†L228-L279】

