#include "fbtest_color.h"
#include "../display.h"
#include "../config.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include "fb_patterns.h"
#include "../keyboard.h"
#include "../cpuidle.h"
#include "../netface.h"
#include <stdint.h>

extern void video_init(void);

static void wait_for_keypress(void) {
    for (;;) {
        int c = keyboard_poll_char();
        if (c >= 0) {
            break;
        }
        netface_poll();
        cpuidle_idle();
    }
}

void fbtest_run(void) {
    console_writeln("fbtest: versuche Framebuffer 640x480/640x400 @ 8bpp. Taste drücken für Abbruch.");

    uint16_t preferred_height = 480;
#if CONFIG_VIDEO_ENABLE_ET4000
    if (CONFIG_VIDEO_ET4000_MODE == CONFIG_VIDEO_ET4000_MODE_640x400x8) {
        preferred_height = 400;
    }
#endif
    int fb_enabled = gpu_request_framebuffer_mode(640, preferred_height, 8);
    if (!fb_enabled && preferred_height != 480) {
        fb_enabled = gpu_request_framebuffer_mode(640, 480, 8);
    }
    if (!fb_enabled) {
        console_writeln("fbtest: kein unterstützter Framebuffer gefunden.");
        return;
    }

    const display_state_t* st = display_manager_state();
    if (!st || !(st->active_features & DISPLAY_FEATURE_FRAMEBUFFER) || !st->active_mode.framebuffer) {
        console_writeln("fbtest: Framebuffer-Zustand unerwartet -> breche ab.");
        gpu_restore_text_mode();
        video_init();
        return;
    }

    volatile uint8_t* fb = st->active_mode.framebuffer;
    uint16_t width = st->active_mode.width;
    uint16_t height = st->active_mode.height;
    uint32_t pitch = st->active_mode.pitch;
    uint8_t bpp = st->active_mode.bpp;

    fb_patterns_configure_palette();
    fb_patterns_draw_demo(fb, width, height, pitch, bpp);

    wait_for_keypress();

    gpu_restore_text_mode();
    video_init();
    console_clear();
    console_writeln("fbtest: Textmodus wieder aktiv.");
    display_manager_log_state();
}
