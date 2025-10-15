# Keyboard Input Fix Notes

_Stand: 2025-10-12_

## Ausgangsproblem
- Nach dem Refactoring der Boot-Stages kamen unter PCem/QEMU keine Tastatureingaben mehr an. Stattdessen tauchten sporadische Zeichen (`$`, `"` …) auf, obwohl keine Taste gedrückt wurde.
- Ursache: Der PS/2‑Controller lieferte weiterhin Maus/Reservierungsbytes über Port `0x60`. Unser einfacher Polling/IRQ-Pfad interpretierte diese als Tastencodes. Zusätzlich waren alte Bytes aus dem BIOS-Setup noch im Buffer.

## Änderungen
1. **keyboard.c**
   - Reset/Drain des 8042-Buffers beim `keyboard_init()`.
   - Ringpuffer + Debug-Historie behalten, aber alle eingehenden Bytes laufen zuerst durch `kbd_log()`.
   - Sowohl Polling- als auch IRQ-Pfad prüfen das Status-Register (`0x64`) auf Bit 0 (`data ready`) und Bit 5 (`mouse packet`). Mausbytes werden verworfen.
   - Neue Helfer `keyboard_status_refresh()` + `console_status_set_mid()` zeigen (optional) den zuletzt gesehenen Scancode (`kbd: 0xNN`) in der Statusleiste.
2. **interrupts.c**
   - IRQ1 liest nun zuerst `0x64`, verwirft Mausdaten und reicht nur Tastatur-Scancodes weiter.
3. **Statusbar-Manager**
   - Die gesamte Statuszeile wird jetzt über `statusbar.c` verwaltet (Slots für Links/Mitte/Rechts mit Prioritäten & Icons). `console_status_set_*()` fungieren nur noch als Legacy-Wrapper.
   - Debug-Slots (z. B. `kbd: 0xNN`) belegen die Center-Gruppe und werden automatisch skaliert, ohne andere Texte zu übermalen.

## Wirkung
- Tastatur arbeitet wieder korrekt auf PCem (486-Clone) und QEMU; keine Zufallszeichen mehr.
- Debugging: `kbdump` in der Shell gibt weiterhin die letzten Roh-Scancodes aus. Zusätzlich kann das Mid-Feld (sofern nicht von Status-Texten überlagert) den aktuellsten Code anzeigen.

## Tipps
- Slots lassen sich jetzt modular belegen – kurzzeitig genutzte Anzeigen (Shell, Apps) sollten eigene Slots registrieren und nach Gebrauch wieder freigeben.
- Bei erneuten Eingabeproblemen zuerst `kbdump` verwenden – erscheinen keine neuen Werte, prüfen ob IRQ1 maskiert ist oder ob der Emulator ein anderes Scancode-Set liefert.
