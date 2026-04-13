#include "fbtest_color.h"
#include "../display.h"
#include "../config.h"
#include "../console.h"
#include "../drivers/gpu/gpu.h"
#include "../drivers/gpu/fb_accel.h"
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
    const gpu_info_t* gpu = gpu_get_primary();
    if (gpu) {
        console_write("fbtest: primäre GPU erkannt: ");
        console_writeln(gpu->name);
    } else {
        console_writeln("fbtest: keine primäre GPU registriert (nutze VGA Fallback).");
    }

    console_writeln("fbtest: versuche Diagnose-Modus (Auto-Selection)...");

    // Strategy: 8bpp preferred, fallback to 4bpp
    int fb_enabled = 0;
    if (gpu && gpu->framebuffer_size >= 300 * 1024) {
        fb_enabled = gpu_request_framebuffer_mode(640, 480, 8);
    }
    
    if (!fb_enabled) {
        fb_enabled = gpu_request_framebuffer_mode(320, 200, 8);
    }
    
    if (!fb_enabled) {
        fb_enabled = gpu_request_framebuffer_mode(640, 480, 4);
    }

    if (!fb_enabled) {
        console_writeln("fbtest: kein unterstützter Grafikmodus gefunden.");
        return;
    }

    const display_state_t* st = display_manager_state();
    if (!st || !(st->active_features & DISPLAY_FEATURE_FRAMEBUFFER) || !st->active_mode.framebuffer) {
        console_writeln("fbtest: Framebuffer-Zustand fehlerhaft.");
        gpu_restore_text_mode();
        video_init();
        return;
    }

    console_write("fbtest: Aktiviert: ");
    console_write_dec(st->active_mode.width);
    console_write("x");
    console_write_dec(st->active_mode.height);
    console_write("x");
    console_write_dec(st->active_mode.bpp);
    console_writeln(" (Beliebige Taste zum Beenden)");

    volatile uint8_t* fb = st->active_mode.framebuffer;
    uint16_t width = st->active_mode.width;
    uint16_t height = st->active_mode.height;
    uint32_t pitch = st->active_mode.pitch;
    uint8_t bpp = st->active_mode.bpp;

    fb_patterns_configure_palette();
    fb_patterns_draw_demo(fb, width, height, pitch, bpp);
    
    // VITAL: Upload shadow buffer to hardware VRAM
    fb_accel_sync();

    wait_for_keypress();

    gpu_restore_text_mode();
    video_init();
    console_clear();
    console_writeln("fbtest: Textmodus wieder aktiv.");
    display_manager_log_state();
}
