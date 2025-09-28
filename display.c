#include "display.h"
#include "console.h"
#include <stddef.h>
#include <stdint.h>
#include <stdint.h>

// interner Zustand, zentral verwaltet
typedef struct {
    display_state_t state;
} display_manager_ctx_t;

static display_manager_ctx_t g_ctx;

static display_mode_info_t make_text_mode(uint16_t columns, uint16_t rows, uint8_t color_count) {
    display_mode_info_t mode;
    mode.kind = DISPLAY_MODE_KIND_TEXT;
    mode.width = columns;
    mode.height = rows;
    mode.bpp = 0;
    mode.pitch = columns;
    mode.pixel_format = (color_count <= 2) ? DISPLAY_PIXEL_FORMAT_TEXT_MONO : DISPLAY_PIXEL_FORMAT_TEXT_16COLOR;
    mode.phys_base = 0;
    mode.framebuffer = NULL;
    return mode;
}

static void reset_mode(display_mode_info_t* mode) {
    if (!mode) return;
    mode->kind = DISPLAY_MODE_KIND_NONE;
    mode->pixel_format = DISPLAY_PIXEL_FORMAT_NONE;
    mode->width = 0;
    mode->height = 0;
    mode->bpp = 0;
    mode->pitch = 0;
    mode->phys_base = 0;
    mode->framebuffer = NULL;
}

void display_manager_init(uint32_t requested_target) {
    g_ctx.state.active_driver_name = "(none)";
    reset_mode(&g_ctx.state.active_mode);
    g_ctx.state.active_features = 0;

    g_ctx.state.text_driver_name = NULL;
    reset_mode(&g_ctx.state.text_mode);

    g_ctx.state.framebuffer_driver_name = NULL;
    reset_mode(&g_ctx.state.framebuffer_mode);

    g_ctx.state.requested_target = requested_target;
}

void display_manager_set_text_mode(const char* driver_name, uint16_t columns, uint16_t rows, uint8_t color_count) {
    g_ctx.state.text_driver_name = driver_name;
    g_ctx.state.text_mode = make_text_mode(columns, rows, color_count);

    // Aktivieren, falls noch nichts gesetzt oder Text ausdrücklich gewünscht ist
    if (g_ctx.state.active_mode.kind == DISPLAY_MODE_KIND_NONE ||
        g_ctx.state.requested_target == DISPLAY_TARGET_TEXT ||
        (g_ctx.state.requested_target == DISPLAY_TARGET_AUTO && !(g_ctx.state.active_features & DISPLAY_FEATURE_FRAMEBUFFER))) {
        display_manager_activate_text();
    }
}

void display_manager_set_framebuffer_candidate(const char* driver_name, const display_mode_info_t* mode) {
    if (!mode || mode->kind != DISPLAY_MODE_KIND_FRAMEBUFFER) {
        // Sicherheitsnetz: falsche Art -> ignorieren
        return;
    }
    g_ctx.state.framebuffer_driver_name = driver_name;
    g_ctx.state.framebuffer_mode = *mode;

    if (g_ctx.state.requested_target == DISPLAY_TARGET_FRAMEBUFFER ||
        (g_ctx.state.requested_target == DISPLAY_TARGET_AUTO && !(g_ctx.state.active_features & DISPLAY_FEATURE_FRAMEBUFFER))) {
        display_manager_activate_framebuffer();
    }
}

void display_manager_activate_text(void) {
    if (!g_ctx.state.text_driver_name || g_ctx.state.text_mode.kind != DISPLAY_MODE_KIND_TEXT) {
        return;
    }
    g_ctx.state.active_driver_name = g_ctx.state.text_driver_name;
    g_ctx.state.active_mode = g_ctx.state.text_mode;
    g_ctx.state.active_features = DISPLAY_FEATURE_TEXT;
}

void display_manager_activate_framebuffer(void) {
    if (!g_ctx.state.framebuffer_driver_name ||
        g_ctx.state.framebuffer_mode.kind != DISPLAY_MODE_KIND_FRAMEBUFFER) {
        return;
    }
    g_ctx.state.active_driver_name = g_ctx.state.framebuffer_driver_name;
    g_ctx.state.active_mode = g_ctx.state.framebuffer_mode;
    g_ctx.state.active_features = DISPLAY_FEATURE_FRAMEBUFFER;
    if (g_ctx.state.text_mode.kind == DISPLAY_MODE_KIND_TEXT) {
        g_ctx.state.active_features |= DISPLAY_FEATURE_TEXT;
    }
}

const display_state_t* display_manager_state(void) {
    return &g_ctx.state;
}

static const char* pixel_format_name(display_pixel_format_t fmt) {
    switch (fmt) {
        case DISPLAY_PIXEL_FORMAT_TEXT_MONO: return "Text (monochrom)";
        case DISPLAY_PIXEL_FORMAT_TEXT_16COLOR: return "Text (16 Farben)";
        case DISPLAY_PIXEL_FORMAT_PAL_256: return "Palettenmodus (256 Farben)";
        case DISPLAY_PIXEL_FORMAT_RGB_565: return "RGB 5:6:5";
        case DISPLAY_PIXEL_FORMAT_RGB_888: return "RGB 8:8:8";
        default: return "unbekannt";
    }
}

void display_manager_log_state(void) {
    const display_state_t* st = &g_ctx.state;
    console_write("Display: aktiv ");
    console_write(st->active_driver_name ? st->active_driver_name : "(unbekannt)");
    console_write(" | Modus: ");
    if (st->active_mode.kind == DISPLAY_MODE_KIND_TEXT) {
        console_write_dec(st->active_mode.width);
        console_write("x");
        console_write_dec(st->active_mode.height);
        console_write(" Zeichen, ");
    } else if (st->active_mode.kind == DISPLAY_MODE_KIND_FRAMEBUFFER) {
        console_write_dec(st->active_mode.width);
        console_write("x");
        console_write_dec(st->active_mode.height);
        console_write(" Pixel @ ");
        console_write_dec(st->active_mode.bpp);
        console_write(" bpp, Pitch ");
        console_write_dec(st->active_mode.pitch);
        console_write(" Bytes");
        console_write(" | ");
    } else {
        console_write("(kein Modus) ");
    }
    console_write("Format: ");
    console_write(pixel_format_name(st->active_mode.pixel_format));
    console_write("\n");

    if ((st->active_features & DISPLAY_FEATURE_FRAMEBUFFER) && st->framebuffer_driver_name) {
        console_write("Display: framebuffer verfügbar via ");
        console_write(st->framebuffer_driver_name);
        console_write(" | phys=0x");
        console_write_hex32(st->framebuffer_mode.phys_base);
        console_write(" ptr=0x");
        console_write_hex32((uint32_t)(uintptr_t)st->framebuffer_mode.framebuffer);
        console_write("\n");
    }
}
