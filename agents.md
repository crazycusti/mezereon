# Agents Notes - Mezereon

Zweck
- Sammelstelle fuer alle Hinweise/Status/ToDos fuer KI-Agenten.
- Ersetzt AI_HINTS.md, TODO.md und notes.md (nur hier pflegen).

## Aktueller Status (2026-04-13) - v0.5.3

### **Modularität & Strategie (Unified Driver Model)**
- **Architektur-Vorgabe:** `GEMINI.md` und `docs/kernel/driver_model.md` erstellt. Fokus: Weg von `if/else`, hin zu `struct device` und Driver-Registries.
- **Nächster Schritt:** Refactoring der `gpu_init` und `main.c` zur Nutzung einer GPU-Registry.

### **Grafik & GPU (ISA/Cirrus/AVGA2)**
- **AVGA2-Treiber:** Dedizierter ISA-Treiber für Cirrus Logic CL-GD5402 (Acumos AVGA2) implementiert.
- **Performance-Boost:** Nutzt jetzt **32-Bit (uint32_t)** Transfers für den VRAM-Sync und minimiert Bank-Umschaltungen (v0.5.3).
- **Layout-Fix:** Interner Konsolen-Puffer auf 60 Zeilen erhöht (behebt das 75%-Höhen-Problem bei 640x480).
- **VRAM-Detection:** Liest VRAM-Größe über SR0F aus (256K, 512K, 1M).
- **Fallback-Logik:** Erkennt automatisch, ob 640x480x8 in den VRAM passt. Falls nicht (bei 256K), erfolgt ein Fallback auf 640x480x16 (Planar) oder 320x200x8.
- **Shadow Buffer Sync:** Nutzt RAM-Schattenpuffer. Shell ruft jetzt `fb_accel_sync()` im Loop auf (v0.5.2).
- **Textmodus-Fix:** Robuste Rückkehr zum Textmodus via Sequencer-Reset und `vga_set_mode3`.

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
