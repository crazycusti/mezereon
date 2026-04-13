# Agents Notes - Mezereon

Zweck
- Sammelstelle fuer alle Hinweise/Status/ToDos fuer KI-Agenten.
- Ersetzt AI_HINTS.md, TODO.md und notes.md (nur hier pflegen).

## Aktueller Status (2026-04-13) - v0.5.4

### **Grafik & GPU (ISA/Cirrus/AVGA2)**
- **v0.5.4 Fix:** Physischer VRAM-Check (Wraparound-Test) implementiert, um Hardware-Lügen über SR0F zu entlarven.
- **Status:** Problem mit Bildspiegelung bei 640x480x8 auf AVGA2 besteht trotz Check weiterhin. Vermutung: Hardware-Bank-Mapping bei 256KB-Karten verhält sich anders als erwartet.
- **Nächster Schritt:** Hardware-Verifikation der RAM-Bestückung (User-seitig). Falls 256KB bestätigt, muss der Fallback-Mechanismus in `avga2_get_vram_size` verfeinert werden.

### **Grafik & GPU (Compaq Aero / SMOS SPC8106)**
- **SMOS SPC8106 Treiber:** Dedizierter Treiber implementiert. Nutzt Register-Unlock (0x1A an Index 0x0E/0x1E an Ports 0x3DE/0x3DF).
- **Auflösungen:**
    - 320x200x256 (Mode 13h) stabil.
    - 640x400x256 (Register-Hack) implementiert.
    - 640x480x4 (Mode 12h) via Planar-Sync unterstützt.
- **Dynamic Terminal Geometry:** Die Konsole berechnet Zeilen/Spalten jetzt dynamisch basierend auf der Hardware-Auflösung.
- **Memory Safety:** Der 300 KB Grafik-Puffer wird jetzt dynamisch via `memory_alloc` im hohen RAM (oberhalb 1 MB) angelegt.

### **System & Stabilität**
- **Stack-Relocation:** Kernel-Stack von 0x9FC00 auf **4 MB** verschoben (in `entry32.asm`).
- **Interrupt Fixes:** `interrupts_save_disable` korrigiert (pushfl/popl Sequenz). Statusbar-Poll mit Bounds-Checking versehen.
- **IDT Cleanup:** Double-Fault Task-Gate entfernt (Rückkehr zu stabilem Standard-Interrupt-Gate in `idt.c`).

### **Serial Debug & Loader**
- **Serial Input:** Bidirektionaler Support für COM1. Shell kann über Serial bedient werden.
- **ANSI Heartbeat:** Heartbeat-Zähler wurde via ANSI-Escapes nach oben rechts (1;65H) verbannt.
- **Hybrid Serial Loader:** Stage 2 bietet beim Booten 'S' für Serial-Boot an. Host-Tool: `tools/serial_upload.py`.

### **Historie / Meilensteine**
- [DONE] Tseng ET4000 8bpp Banking optimiert.
- [DONE] Acumos AVGA2 (Cirrus) identifiziert und VGA-Pfad stabilisiert.
- [DONE] Compaq Aero (SMOS) Hardware-Unlock & Revision-Check (F0B).
- [DONE] Shell-Bedienbarkeit im Framebuffer (Dynamic Grid).

### **Bekannte Offene Punkte**
- [ ] 640x400x256 CRTC-Timings auf echtem Aero-LCD verifizieren.
- [ ] Hintergrund-Sync (fb_accel) Performance-Optimierung für 33MHz i486.
- [ ] PCI-Scanning: SMOS wird als ISA/Legacy erkannt.

## Projektueberblick (Kurz)
- Bootpfad: stage1/2/3 -> kernel.
- VGA-Standard als Fallback (Mode 13h / Mode 12h).
- Debugging primär über COM1 (Serial) mit bidirektionaler Shell.
