MezAPI Grafikzugriff (Framebuffer)
==================================

Überblick
---------
- Das Kernel stellt ab `MEZ_ABI32_V1` ein optionales Framebuffer-Feature zur Verfügung.
- Unterstützung erkennt man am Flag `MEZ_CAP_VIDEO_FB` im Feld `capabilities` der API-Tabelle.
- Der Framebuffer entspricht dem aktuellen Konsolen-Backend (z. B. Cirrus Linear Framebuffer 640×480×8).
- Falls kein Framebuffer aktiv ist (reiner Textmodus oder andere Architektur), bleibt das Flag 0 und `video_fb_get_info()` liefert `NULL`.

Strukturen & Funktionen
-----------------------
```c
#define MEZ_CAP_VIDEO_FB       (1u << 0)
#define MEZ_CAP_VIDEO_FB_ACCEL (1u << 1)

typedef struct {
    uint16_t width;       // Pixelbreite
    uint16_t height;      // Pixelhöhe
    uint32_t pitch;       // Bytes pro Zeile
    uint8_t  bpp;         // Bits pro Pixel (derzeit 8)
    const void* framebuffer; // Pointer auf den Anfang des LFB
} mez_fb_info32_t;

const mez_fb_info32_t* (*video_fb_get_info)(void);
void (*video_fb_fill_rect)(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);
```

Nutzungsschritte
----------------
1. `mez_api_get()` aufrufen und `capabilities & MEZ_CAP_VIDEO_FB` prüfen.
2. Wenn gesetzt, `api->video_fb_get_info()` aufrufen. Liefert `NULL`, falls das Backend zwischenzeitlich wieder in den Textmodus gewechselt ist.
3. Breite, Höhe, Pitch und bpp auswerten. Bei 8 bpp handelt es sich um Palettenindizes (Standard-VGA-Palette wird beim Umschalten geladen).
4. Malen wie gewohnt: `framebuffer[y * pitch + x] = farbindex;`
5. Optional: Bei `MEZ_CAP_VIDEO_FB_ACCEL` steht `video_fb_fill_rect()` als schnelle Flächenfüllung zur Verfügung. Der Kernel nutzt dabei Hardwarebeschleunigung (z. B. Cirrus BitBLT) und fällt sonst auf eine CPU-Schleife zurück.

Minimalbeispiel
---------------
```c
#include "mezapi.h"

void fb_draw_demo(const mez_api32_t* api) {
    if (!(api->capabilities & MEZ_CAP_VIDEO_FB) || !api->video_fb_get_info)
        return; // Kein Framebuffer verfügbar

    const mez_fb_info32_t* fb = api->video_fb_get_info();
    if (!fb || fb->bpp != 8) return;

    if ((api->capabilities & MEZ_CAP_VIDEO_FB_ACCEL) && api->video_fb_fill_rect) {
        for (uint16_t band = 0; band < fb->width; band += 16) {
            api->video_fb_fill_rect(band, 0, 16, fb->height, (uint8_t)((band / 16) & 0xFF));
        }
    } else {
        uint8_t* base = (uint8_t*)fb->framebuffer;
        for (uint16_t y = 0; y < fb->height; y++) {
            for (uint16_t x = 0; x < fb->width; x++) {
                base[y * fb->pitch + x] = (uint8_t)((x / 16) & 0xFF);
            }
        }
    }
}
```

Fallbacks & Hinweise
--------------------
- Wenn der Kernel zurück in den Textmodus wechselt (z. B. Alt-Tab, Debug-Ausgabe), kann `video_fb_get_info()` plötzlich `NULL` liefern. Applikationen sollten diese Möglichkeit abfangen.
- Auf anderen Architekturen (SPARC, zukünftige Ports) existiert derzeit kein Framebuffer-Backend; das API verhält sich dann wie zuvor.
- Paletten-Updates sind momentan nicht über MezAPI verfügbar. Es wird die Standard-VGA-Palette verwendet, die der Kernel beim Umschalten setzt.
- Der Framebuffer teilt sich den Speicher mit der Kernel-Konsole. Jede Anwendung sollte daher vorsichtig sein und z. B. nur in eigenen Bereichen schreiben oder den Bildschirm vollständig neu befüllen.

Weiterführend
-------------
- `docs/api/mezapi.md` — Gesamte API-Referenz
- `docs/hw/pci_gpu.md` — Hinweise zu Grafikhardware und `gpuinfo`
- `apps/fbtest_color.c` — Farbbalken-Demo im Kernel (zeigt, wie Palette und Framebuffer genutzt werden)
- `apps/rotcube_app.c` — MezAPI-Demo, die die beschleunigte `video_fb_fill_rect()` nutzt
- `apps/mezcompose_app.c` — Fenster-Layout-Vorschau, die Framebuffer und Textmodus synchron hält
