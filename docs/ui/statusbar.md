# Status Bar System

_Stand: 2025-10-12_

## Überblick
- Die komplette oberste Bildschirmzeile gehört der Statusbar. Sie wird nicht gescrollt und behält einen blauen Hintergrund (Attr `0x1F`).
- Inhalte werden über _Slots_ organisiert:
  - **Position:** `left`, `center`, `right`
  - **Priorität:** bestimmt Reihenfolge / Kürzungsreihenfolge innerhalb einer Gruppe
  - **Icon (optional):** kann bei Platzmangel alleine stehen bleiben (`STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE`)
- Der `statusbar`-Manager übernimmt Layout, Kürzung & Übergabe an das Backend (`video_status_draw_full`).

## Kernel-API
```c
#include "statusbar.h"

void statusbar_init(void);               // einmalig (vor console_init)
void statusbar_backend_ready(void);      // wird von console_init aufgerufen

statusbar_slot_t statusbar_register(const statusbar_slot_desc_t* desc);
void statusbar_set_text(statusbar_slot_t slot, const char* text);
void statusbar_set_icon(statusbar_slot_t slot, char icon);
void statusbar_release(statusbar_slot_t slot);
```

- Legacy-Helfer (`statusbar_legacy_set_left/mid/right`) speisen die alten `console_status_*` Wrapper.
- Beispiel Kernel-Slot (GFX-Modus):
  ```c
  static statusbar_slot_t s_gfx_slot;
  s_gfx_slot = statusbar_register(&(statusbar_slot_desc_t){
      .position = STATUSBAR_POS_CENTER,
      .priority = 90,
      .flags = STATUSBAR_FLAG_ICON_ONLY_ON_TRUNCATE,
      .icon = 'G',
      .initial_text = "gfx: text"
  });
  statusbar_set_text(s_gfx_slot, "gfx: framebuffer");
  ```

## MezAPI (Apps)
- Neue Funktionen ab `mez_api32_t`:
  ```c
  mez_status_slot_t (*status_register)(mez_status_pos_t pos,
                                       uint8_t priority,
                                       uint8_t flags,
                                       char icon,
                                       const char* initial_text);
  void (*status_update)(mez_status_slot_t slot, const char* text);
  void (*status_release)(mez_status_slot_t slot);
  ```
- Typen in `mezapi.h`:
  ```c
  typedef enum { MEZ_STATUS_POS_LEFT, MEZ_STATUS_POS_CENTER, MEZ_STATUS_POS_RIGHT } mez_status_pos_t;
  #define MEZ_STATUS_FLAG_ICON_ONLY_ON_TRUNCATE 0x01u
  #define MEZ_STATUS_SLOT_INVALID 0xFF
  ```
- Apps sollten Slots **freigeben**, wenn nicht mehr benötigt (`status_release`), damit Platz für andere Daten bleibt.

## Layout-Details
- Links und Rechts werden streng von außen nach innen gefüllt. Die Mitte hängt sich direkt an den linken Bereich an – falls links leer ist, erscheinen mittlere Slots ganz links, ansonsten mit einem einzelnem Abstand.
- Strings werden bei Bedarf abgeschnitten. Slots mit Icon-Flag zeigen dann nur das Symbol.
- Alle Berechnungen basieren auf 80 Spalten (Konfiguration `CONFIG_VGA_WIDTH`).

## Tipps & Best Practices
- Kurze, prägnante Texte verwenden; mehrere Slots lieber in Center-Gruppe mit verschiedenen Prioritäten organisieren.
- Debug-Anzeigen (z. B. `kbd: 0xNN`) sollten über niedrige Prioritäten laufen, damit produk­tive Infos Vorrang haben.
- Bei Framebuffer-Ausgabe bleibt der farbige Verlauf in Zeile 0 erhalten; Status-Texte werden darauf gerendert.
