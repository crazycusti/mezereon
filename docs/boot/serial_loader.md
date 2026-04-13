# Serial Kernel Loader (Hybrid Boot)

Der Mezereon Serial Loader erlaubt es, den Kernel während des Bootvorgangs über die serielle Schnittstelle (COM1) nachzuladen, ohne das Disketten-Image neu schreiben zu müssen.

## Funktionsweise
1. **Stage 2:** Bietet beim Start ein Zeitfenster von 2 Sekunden an (`Press [S] for Serial Loader`).
2. **Stage 3:** Falls 'S' gedrückt wurde, wechselt Stage 3 in den Empfangsmodus.
3. **Protokoll:** Einfache Übertragung der Dateigröße (4 Bytes, Little Endian), gefolgt vom rohen Binary.
4. **Execution:** Sobald der Transfer abgeschlossen ist, wird der Kernel sofort gestartet.

## Voraussetzungen
- Serielles Kabel (Nullmodem) am Aero (COM1).
- Python 3 und `pyserial` auf dem Host-Rechner.

## Anwendung
1. Aero einschalten und beim Erscheinen der Meldung **'S'** drücken.
2. Auf dem Host-Rechner (MacBook/PC) das Upload-Script ausführen:
   ```bash
   python3 tools/serial_upload.py /dev/cu.usbserial-XXX kernel_payload.bin
   ```
3. Der Fortschrittsbalken zeigt den Status an. Nach "Done" startet der Aero den neuen Kernel.

## Debugging
- Die Baudrate ist fest auf **115200 8N1** eingestellt.
- Falls der Transfer nicht startet, Kabelverbindung und Port-Name am Host prüfen.
