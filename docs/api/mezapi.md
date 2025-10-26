MezAPI (in-kernel application API)

Overview
- `mez_api_get()` exposes a stable table of function pointers for small user applications linked into the kernel image.
- ABI identifier: `MEZ_ABI32_V1` for 32-bit x86 builds. Future revisions append members but keep existing layout.

Provided services
- Console: `console_write`, `console_writeln`, `console_clear`
- Input: non-blocking `input_poll_key()`
- Timing: `time_ticks_get()`, `time_timer_hz()`, `time_sleep_ms()`
- Sound: `sound_beep(hz, ms)`, `sound_tone_on(hz)`, `sound_tone_off()` sowie `sound_get_info()` → `mez_sound_info32_t` mit Backends (`MEZ_SOUND_BACKEND_PCSPK`, `MEZ_SOUND_BACKEND_SB16`), SB16-Basisport/IRQ/DMA/Version; `MEZ_CAP_SOUND_SB16` signalisiert erkannte SB16-Hardware
- Text mode helpers: `text_put(x,y,ch,attr)`, `text_fill_line(y,ch,attr)`
- Statusbar:
  - Legacy Wrapper: `status_left(text)`, `status_right(text,len)`
  - Slots: `status_register(pos, priority, flags, icon, initial_text)`, `status_update(slot, text)`, `status_release(slot)`
  - Position enum `mez_status_pos_t` (`LEFT/CENTER/RIGHT`), Flags (`MEZ_STATUS_FLAG_ICON_ONLY_ON_TRUNCATE`)
- Framebuffer: `capabilities` bitmask (`MEZ_CAP_VIDEO_FB`, `MEZ_CAP_VIDEO_FB_ACCEL`), `video_fb_get_info()` → returns `NULL` oder `mez_fb_info32_t` (Breite, Höhe, Pitch, bpp, `framebuffer`), `video_fb_fill_rect(x,y,w,h,color)` für schnelle Flächenfüllungen (setzt `MEZ_CAP_VIDEO_FB_ACCEL` voraus).
- GPU-Metadaten: `video_gpu_get_info()` liefert `mez_gpu_info32_t` (Featurelevel, Adaptertyp, CAP-Flags). `MEZ_CAP_VIDEO_GPU_INFO` signalisiert, dass der Kernel mindestens den Textmodus beschreibt; Featurelevel > `MEZ_GPU_FEATURELEVEL_TEXTMODE` stehen für erkannte Framebuffer-Hardware (Cirrus, Tseng, Acumos AVGA2).

Usage pattern
1. Call `mez_api_get()` and verify `abi_version >= MEZ_ABI32_V1` and `arch == MEZ_ARCH_X86_32`.
2. Optionally check `size` to guard against future additions.
3. Store the pointer table and call helpers as needed.

Example
```c
#include "mezapi.h"

int sample_app(const mez_api32_t* api) {
    if (!api || api->abi_version < MEZ_ABI32_V1) return -1;
    api->console_writeln("Hello from MezAPI app!\nPress Ctrl+Q to exit.");
    while (1) {
        int ch = api->input_poll_key ? api->input_poll_key() : -1;
        if (ch == 0x11) break; // Ctrl+Q
        if (ch >= 0) api->sound_beep(440, 100);
    }
    return 0;
}
```

Statusbar-Slot verwenden
```c
mez_status_slot_t slot = MEZ_STATUS_SLOT_INVALID;
if (api->status_register) {
    slot = api->status_register(MEZ_STATUS_POS_CENTER,
                                80,                      // hohe Priorität
                                MEZ_STATUS_FLAG_ICON_ONLY_ON_TRUNCATE,
                                '*',
                                "demo ready");
}

if (slot != MEZ_STATUS_SLOT_INVALID) {
    api->status_update(slot, "processing...");
    // ... Arbeit verrichten ...
    api->status_release(slot);
}
```

Sound backend query
```c
const mez_sound_info32_t* snd = api->sound_get_info ? api->sound_get_info() : NULL;
if (snd && (snd->backends & MEZ_SOUND_BACKEND_SB16)) {
    api->console_writeln("SB16 detected via MezAPI.");
}
```

GPU feature levels
```c
const mez_gpu_info32_t* gpu = api->video_gpu_get_info ? api->video_gpu_get_info() : NULL;
if (gpu && gpu->feature_level >= MEZ_GPU_FEATURELEVEL_BANKED_FB) {
    api->console_write("GPU: ");
    api->console_writeln(gpu->name);
    switch (gpu->feature_level) {
        case MEZ_GPU_FEATURELEVEL_LINEAR_FB_ACCEL:
            api->console_writeln("linear framebuffer + 2D accel");
            break;
        case MEZ_GPU_FEATURELEVEL_LINEAR_FB:
            api->console_writeln("linear framebuffer (no accel)");
            break;
        case MEZ_GPU_FEATURELEVEL_BANKED_FB_ACCEL:
            api->console_writeln("banked framebuffer + AX accel");
            break;
        default:
            api->console_writeln("banked framebuffer window");
            break;
    }
}
```

Featurelevel-Klassifizierung
- `MEZ_GPU_FEATURELEVEL_TEXTMODE`: Nur Textmodus aktiv (z. B. Standard-VGA Mode 3).
- `MEZ_GPU_FEATURELEVEL_BANKED_FB`: 64-KiB-Fenster für Framebuffer, kein Linear-Frame (Tseng ET4000, Acumos AVGA2).
- `MEZ_GPU_FEATURELEVEL_BANKED_FB_ACCEL`: wie oben, jedoch mit AX-Beschleuniger (ET4000AX).
- `MEZ_GPU_FEATURELEVEL_LINEAR_FB`: Linearer Framebuffer ohne spezielle 2D-Einheit.
- `MEZ_GPU_FEATURELEVEL_LINEAR_FB_ACCEL`: Linearer Framebuffer mit 2D-Beschleuniger (z. B. Cirrus BitBLT).

Framebuffer usage
```c
const mez_fb_info32_t* fb = api->video_fb_get_info ? api->video_fb_get_info() : NULL;
if (fb && fb->bpp == 8) {
    if ((api->capabilities & MEZ_CAP_VIDEO_FB_ACCEL) && api->video_fb_fill_rect) {
        api->video_fb_fill_rect(0, 10, fb->width, 1, 0x4F);
    } else {
        uint8_t* ptr = (uint8_t*)fb->framebuffer;
        for (uint16_t x = 0; x < fb->width; x++) {
            ptr[10 * fb->pitch + x] = 0x4F;
        }
    }
}
```

See also
- `apps/keymusic_app.c` oder `apps/rotcube_app.c` für Beispielanwendungen
- `docs/shell/apps.md` für den Shell-Launcher
