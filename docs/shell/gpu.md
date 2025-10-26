GPU shell commands
==================

`gpuinfo`
---------
- `gpuinfo` zeigt alle registrierten Grafikadapter inklusive Bus/Device/Function, Vendor-/Device-ID, berechneter BAR-Größen sowie Fähigkeitsflags (`linear-fb`, `2d-accel`, `hw-cursor`, `vbe-bios`).
- `gpuinfo detail` ergänzt Register-Dumps (Sequencer, CRTC, Graphics, Attribute) und eignet sich für Low-Level-Debugging.
- Beide Varianten laufen vollständig im Textmodus; ein aktiver Framebuffer wird vorher automatisch zurückgesetzt.

`gpuprobe`
---------
- Syntax: `gpuprobe [scan|noscan] [auto|noauto] [status] [debug <on|off>] [activate <chip> <WxHxB>]`
- Standardlauf (ohne Argumente): führt Legacy-Scan + PCI-Diagnose aus, protokolliert Adapter, zeigt Tseng-Autopilot-Status und verweist auf Fehlerspeicher.
- `activate <chip> <WxHxB>` erzwingt eine Framebuffer-Aktivierung. Unterstützte `chip`-Tokens: `et4000`, `et4000ax`, `avga2`, `cirrus gd5446`, `vga`. Der ältere Alias `cirrus` bleibt als Eingabe erhalten; empfohlen ist die Variante `cirrus gd5446`, da jede Cirrus-Karte eigene Register-Programme benötigt.
- Vor der eigentlichen Umschaltung blendet `gpuprobe` alle bekannten Framebuffer-Modi für den gewählten Chip ein (z. B. `640x480x8 bpp`). Die Ausgabe enthält bereits den passenden `gpuprobe activate <chip> ...`-Aufruf mit dem konkreten Chip-Token, sodass sich eine passende Auflösung/Farbtiefe auswählen lässt, bevor der Framebuffer scharf geschaltet wird.
- Fehlende oder ungültige Modusangaben (`640x400x8`, `800x600x8`, …) werden abgefangen; das Tool erinnert daran, mit `gpuprobe activate <chip> <WxHxB>` zu wiederholen.
- `status` gibt den zuletzt aktivierten Modus und die Autokonfiguration aus; `debug on|off` steuert zusätzliche Register-Dumps.
- Bei vorhandenen PCI-GPUs (z. B. Cirrus GD5446 unter QEMU) wird die Tseng-Erkennung nicht mehr automatisch in die Gerätetabelle eingetragen. `gpuprobe scan` registriert bei Bedarf dennoch gefundene ET4000/AVGA2-Adapter, damit anschließend u. a. `640x480x4` in der Modusliste erscheint.

`gpudump`
---------
- `gpudump` ohne Argumente ermittelt den bevorzugten Grafikadapter (MezAPI `video_gpu_get_info()` bzw. erste Geräteliste) und gibt einen vollständigen Register-Dump aus (Sequencer, CRTC, Graphics, Attribute, Misc). Unterstützt sind aktuell Cirrus GD5446, Tseng ET4000/AX sowie Acumos AVGA2.
- `gpudump regs <chip>` erzwingt einen bestimmten Adapter (`cirrus`, `cirrus-gd5446`, `et4000`, `et4000ax`, `avga2`, `vga`), `gpudump regs all` iteriert über alle erkannten Karten, `gpudump auto` bzw. `gpudump regs auto` entspricht der Standardausgabe.
- Für Legacy-Banked-Framebuffer bleibt `gpudump bank <bank> [offset] [len]` erhalten; `gpudump capture <bank> [offset] [len]` triggert den optionalen ET4000AX-VRAM-Schnappschuss.

`fbtest`
--------
- `fbtest` versucht den Framebuffer im Kernel zu aktivieren und zeigt Farbbalken; eignet sich nach erfolgreichem `gpuprobe activate` zur schnellen Sichtkontrolle.
- `gpu_restore_text_mode` wird am Ende aufgerufen, sodass der Shell-Textmodus erhalten bleibt.

`gfxprobe`
---------
- `gfxprobe` lädt bei Bedarf automatisch den bevorzugten Framebuffer-Modus (Standard: 640×480×8), setzt die VGA-Palette zurück und zeichnet ein Testpattern.
- Nach einer Tasteneingabe wird der Textmodus wiederhergestellt; Ausgabe und Statusleiste bestätigen die Rückkehr in den sicheren Textbetrieb.

Siehe zusätzlich `docs/hw/pci_gpu.md` für Hintergrundinformationen zu den Treibern und Feature-Levels.
