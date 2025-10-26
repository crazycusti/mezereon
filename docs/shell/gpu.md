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
- `activate <chip> <WxHxB>` erzwingt eine Framebuffer-Aktivierung. Unterstützte `chip`-Tokens: `et4000`, `et4000ax`, `avga2`, `cirrus`, `vga`.
- Vor der eigentlichen Umschaltung blendet `gpuprobe` alle bekannten Framebuffer-Modi für den gewählten Chip ein (z. B. `640x480x8 bpp`). Die Ausgabe enthält bereits den passenden `gpuprobe activate <chip> ...`-Aufruf mit dem konkreten Chip-Token, sodass sich eine passende Auflösung/Farbtiefe auswählen lässt, bevor der Framebuffer scharf geschaltet wird.
- Fehlende oder ungültige Modusangaben (`640x400x8`, `800x600x8`, …) werden abgefangen; das Tool erinnert daran, mit `gpuprobe activate <chip> <WxHxB>` zu wiederholen.
- `status` gibt den zuletzt aktivierten Modus und die Autokonfiguration aus; `debug on|off` steuert zusätzliche Register-Dumps.

`gpudump`
---------
- `gpudump <bank> [offset] [len]` liest das aktuell gemappte Framebuffer-Bankfenster (Tseng/AVGA) aus.
- Ohne aktivierten Framebuffer liefert der Befehl eine Fehlermeldung.

`fbtest`
--------
- `fbtest` versucht den Framebuffer im Kernel zu aktivieren und zeigt Farbbalken; eignet sich nach erfolgreichem `gpuprobe activate` zur schnellen Sichtkontrolle.
- `gpu_restore_text_mode` wird am Ende aufgerufen, sodass der Shell-Textmodus erhalten bleibt.

Siehe zusätzlich `docs/hw/pci_gpu.md` für Hintergrundinformationen zu den Treibern und Feature-Levels.
