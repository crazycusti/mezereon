#include "gfx_probe.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include "../display.h"
#include "../keyboard.h"
#include "../cpuidle.h"
#include "../netface.h"
#include "fb_patterns.h"

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

static void restore_text_mode(void) {
    gpu_restore_text_mode();
    video_init();
    console_writeln("gfxprobe: Textmodus wieder aktiv.");
    display_manager_log_state();
}

void gfx_probe_run(void) {
    console_writeln("gfxprobe: aktiviere Framebuffer-Test (640x480x8 bevorzugt).");

    int fb_ok = gpu_request_framebuffer_mode(640, 480, 8);
    if (!fb_ok) {
        console_write("gfxprobe: Aktivierung fehlgeschlagen: ");
        console_writeln(gpu_get_last_error());
        return;
    }

    const display_state_t* st = display_manager_state();
    if (!st || !(st->active_features & DISPLAY_FEATURE_FRAMEBUFFER) || !st->active_mode.framebuffer) {
        console_writeln("gfxprobe: Framebuffer nach Aktivierung nicht verfügbar.");
        restore_text_mode();
        return;
    }

    volatile uint8_t* fb = st->active_mode.framebuffer;
    uint16_t width = st->active_mode.width;
    uint16_t height = st->active_mode.height;
    uint32_t pitch = st->active_mode.pitch;
    uint8_t bpp = st->active_mode.bpp;

    console_write("gfxprobe: aktiver Modus ");
    console_write_dec(width);
    console_write("x");
    console_write_dec(height);
    console_write("x");
    console_write_dec(bpp);
    console_writeln(".");

    fb_patterns_configure_palette();
    fb_patterns_draw_demo(fb, width, height, pitch, bpp);

    console_writeln("gfxprobe: Testpattern gezeichnet. Taste drücken zum Zurückkehren in den Textmodus.");
    wait_for_keypress();

    restore_text_mode();
}
