MezAPI (in-kernel application API)

Overview
- `mez_api_get()` exposes a stable table of function pointers for small user applications linked into the kernel image.
- ABI identifier: `MEZ_ABI32_V1` for 32-bit x86 builds. Future revisions append members but keep existing layout.

Provided services
- Console: `console_write`, `console_writeln`, `console_clear`
- Input: non-blocking `input_poll_key()`
- Timing: `time_ticks_get()`, `time_timer_hz()`, `time_sleep_ms()`
- Sound: `sound_beep(hz, ms)`, `sound_tone_on(hz)`, `sound_tone_off()`
- Text mode helpers: `text_put(x,y,ch,attr)`, `text_fill_line(y,ch,attr)`, `status_left(text)`, `status_right(text,len)`

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

See also
- `apps/keymusic_app.c` for a working application
- `docs/shell/apps.md` for the shell-side launcher
