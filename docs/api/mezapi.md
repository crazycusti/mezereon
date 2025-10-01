MezAPI (in-kernel application API)

Overview
- `mez_api_get()` exposes a stable table of function pointers for small user applications linked into the kernel image.
- ABI identifier: `MEZ_ABI32_V1` for 32-bit x86 builds. Future revisions append members but keep existing layout.

Provided services
- Console: `console_write`, `console_writeln`, `console_clear`
- Input: non-blocking `input_poll_key()`
- Timing: `time_ticks_get()`, `time_timer_hz()`, `time_sleep_ms()`
- Sound: `sound_beep(hz, ms)`, `sound_tone_on(hz)`, `sound_tone_off()` sowie `sound_get_info()` → `mez_sound_info32_t` mit Backends (`MEZ_SOUND_BACKEND_PCSPK`, `MEZ_SOUND_BACKEND_SB16`), SB16-Basisport/IRQ/DMA/Version; `MEZ_CAP_SOUND_SB16` signalisiert erkannte SB16-Hardware
- Text mode helpers: `text_put(x,y,ch,attr)`, `text_fill_line(y,ch,attr)`, `status_left(text)`, `status_right(text,len)`
- Framebuffer: `capabilities` bitmask (`MEZ_CAP_VIDEO_FB`, `MEZ_CAP_VIDEO_FB_ACCEL`), `video_fb_get_info()` → returns `NULL` oder `mez_fb_info32_t` (Breite, Höhe, Pitch, bpp, `framebuffer`), `video_fb_fill_rect(x,y,w,h,color)` für schnelle Flächenfüllungen (setzt `MEZ_CAP_VIDEO_FB_ACCEL` voraus).

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

Sound backend query
```c
const mez_sound_info32_t* snd = api->sound_get_info ? api->sound_get_info() : NULL;
if (snd && (snd->backends & MEZ_SOUND_BACKEND_SB16)) {
    api->console_writeln("SB16 detected via MezAPI.");
}
```

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
