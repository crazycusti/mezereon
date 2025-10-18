#include "../config.h"
#include "../mezapi.h"
#include "../statusbar.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if CONFIG_MEZCOMPOSE_ENABLE

#ifndef MEZCOMP_TEXT_ROWS
#ifdef CONFIG_VGA_HEIGHT
#define MEZCOMP_TEXT_ROWS CONFIG_VGA_HEIGHT
#else
#define MEZCOMP_TEXT_ROWS 25
#endif
#endif

#define MEZCOMP_STATUS_COLS STATUSBAR_COLS

typedef struct {
    const char* title;
    const char* lines[6];
    uint8_t line_count;
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
} mezcomp_window_t;

typedef struct {
    const mez_api32_t* api;
    const mez_fb_info32_t* info;
    volatile uint8_t* base;
    int pitch;
    int width;
    int height;
    bool has_fb;
    bool has_accel;
} mezcomp_fb_ctx_t;

static const mezcomp_window_t kMezcompWindows[] = {
    {
        .title = "WORKSPACE",
        .lines = {
            "mezcompose preview",
            "focus rotates automatically",
            "use ESC or Ctrl+Q to exit",
            "mezAPI framebuffer + text"
        },
        .line_count = 4,
        .x = 2,
        .y = 2,
        .w = 36,
        .h = 9,
    },
    {
        .title = "SYSTEM",
        .lines = {
            "cpu load   : balanced",
            "tasks      : 3 windows",
            "scheduler  : spiral",
            "next swap  : +1.5s"
        },
        .line_count = 4,
        .x = 44,
        .y = 2,
        .w = 34,
        .h = 9,
    },
    {
        .title = "MESSAGES",
        .lines = {
            "mezcompose synchronises",
            "framebuffer + text",
            "toggle focus to preview",
            "future hooks via mezapi"
        },
        .line_count = 4,
        .x = 2,
        .y = 12,
        .w = 76,
        .h = 9,
    }
};

static bool mezcomp_fb_prepare(const mez_api32_t* api, mezcomp_fb_ctx_t* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->api = api;
    if (!api || !(api->capabilities & MEZ_CAP_VIDEO_FB) || !api->video_fb_get_info) {
        return false;
    }
    const mez_fb_info32_t* fb = api->video_fb_get_info();
    if (!fb || !fb->framebuffer || fb->bpp != 8 || !fb->pitch || !fb->width || !fb->height) {
        return false;
    }
    ctx->info = fb;
    ctx->base = (volatile uint8_t*)fb->framebuffer;
    ctx->pitch = (int)fb->pitch;
    ctx->width = fb->width;
    ctx->height = fb->height;
    ctx->has_fb = true;
    ctx->has_accel = ((api->capabilities & MEZ_CAP_VIDEO_FB_ACCEL) && api->video_fb_fill_rect);
    return true;
}

static void mezcomp_fb_fill_rect(const mezcomp_fb_ctx_t* ctx, int x, int y, int w, int h, uint8_t color) {
    if (!ctx || !ctx->has_fb || w <= 0 || h <= 0) return;
    if (x >= ctx->width || y >= ctx->height) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    if (ctx->has_accel) {
        ctx->api->video_fb_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
        return;
    }

    volatile uint8_t* base = ctx->base + (uint32_t)y * (uint32_t)ctx->pitch + (uint32_t)x;
    for (int yy = 0; yy < h; ++yy) {
        volatile uint8_t* line = base + (uint32_t)yy * (uint32_t)ctx->pitch;
        for (int xx = 0; xx < w; ++xx) {
            line[xx] = color;
        }
    }
}

static void mezcomp_fb_draw_border(const mezcomp_fb_ctx_t* ctx, int x, int y, int w, int h, uint8_t color) {
    mezcomp_fb_fill_rect(ctx, x, y, w, 1, color);
    mezcomp_fb_fill_rect(ctx, x, y + h - 1, w, 1, color);
    mezcomp_fb_fill_rect(ctx, x, y, 1, h, color);
    mezcomp_fb_fill_rect(ctx, x + w - 1, y, 1, h, color);
}

typedef struct {
    char ch;
    uint8_t rows[7];
} mezcomp_glyph_t;

#define MEZCOMP_GLYPH(ch, r0, r1, r2, r3, r4, r5, r6) \
    { ch, { r0, r1, r2, r3, r4, r5, r6 } }

static const mezcomp_glyph_t kMezcompFont[] = {
    MEZCOMP_GLYPH('A', 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('B', 0x3E, 0x33, 0x3E, 0x33, 0x33, 0x3E, 0x00),
    MEZCOMP_GLYPH('C', 0x1E, 0x33, 0x30, 0x30, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('D', 0x3C, 0x36, 0x33, 0x33, 0x36, 0x3C, 0x00),
    MEZCOMP_GLYPH('E', 0x3F, 0x30, 0x3E, 0x30, 0x30, 0x3F, 0x00),
    MEZCOMP_GLYPH('F', 0x3F, 0x30, 0x3E, 0x30, 0x30, 0x30, 0x00),
    MEZCOMP_GLYPH('G', 0x1E, 0x33, 0x30, 0x37, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('H', 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('I', 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    MEZCOMP_GLYPH('J', 0x07, 0x03, 0x03, 0x03, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('K', 0x33, 0x36, 0x3C, 0x36, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('L', 0x30, 0x30, 0x30, 0x30, 0x30, 0x3F, 0x00),
    MEZCOMP_GLYPH('M', 0x33, 0x3F, 0x3F, 0x33, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('N', 0x33, 0x3B, 0x3F, 0x37, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('O', 0x1E, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('P', 0x3E, 0x33, 0x33, 0x3E, 0x30, 0x30, 0x00),
    MEZCOMP_GLYPH('Q', 0x1E, 0x33, 0x33, 0x33, 0x37, 0x1F, 0x00),
    MEZCOMP_GLYPH('R', 0x3E, 0x33, 0x33, 0x3E, 0x36, 0x33, 0x00),
    MEZCOMP_GLYPH('S', 0x1F, 0x30, 0x1E, 0x03, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('T', 0x3F, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x00),
    MEZCOMP_GLYPH('U', 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('V', 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00),
    MEZCOMP_GLYPH('W', 0x33, 0x33, 0x33, 0x3F, 0x3F, 0x33, 0x00),
    MEZCOMP_GLYPH('X', 0x33, 0x33, 0x1E, 0x1E, 0x33, 0x33, 0x00),
    MEZCOMP_GLYPH('Y', 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x0C, 0x00),
    MEZCOMP_GLYPH('Z', 0x3F, 0x03, 0x0E, 0x18, 0x30, 0x3F, 0x00),
    MEZCOMP_GLYPH('0', 0x1E, 0x33, 0x37, 0x3B, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('1', 0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    MEZCOMP_GLYPH('2', 0x1E, 0x33, 0x03, 0x0E, 0x18, 0x3F, 0x00),
    MEZCOMP_GLYPH('3', 0x3F, 0x03, 0x0E, 0x03, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('4', 0x06, 0x0E, 0x1E, 0x36, 0x3F, 0x06, 0x00),
    MEZCOMP_GLYPH('5', 0x3F, 0x30, 0x3E, 0x03, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('6', 0x1E, 0x30, 0x3E, 0x33, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('7', 0x3F, 0x03, 0x06, 0x0C, 0x18, 0x18, 0x00),
    MEZCOMP_GLYPH('8', 0x1E, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00),
    MEZCOMP_GLYPH('9', 0x1E, 0x33, 0x33, 0x1F, 0x03, 0x1E, 0x00),
    MEZCOMP_GLYPH(':', 0x00, 0x0C, 0x00, 0x00, 0x0C, 0x00, 0x00),
    MEZCOMP_GLYPH('-', 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00),
    MEZCOMP_GLYPH('+', 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00),
    MEZCOMP_GLYPH('/', 0x03, 0x06, 0x0C, 0x18, 0x30, 0x20, 0x00),
    MEZCOMP_GLYPH(' ', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)
};

static const mezcomp_glyph_t* mezcomp_find_glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    const size_t count = sizeof(kMezcompFont) / sizeof(kMezcompFont[0]);
    const mezcomp_glyph_t* space = &kMezcompFont[count - 1];
    for (size_t i = 0; i < count; ++i) {
        if (kMezcompFont[i].ch == ch) {
            return &kMezcompFont[i];
        }
    }
    return space;
}

static void mezcomp_status_snapshot(const mez_api32_t* api, char* out, size_t count) {
    if (!out || count == 0) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = ' ';
    }
    if (api && api->status_snapshot) {
        size_t written = api->status_snapshot(out, count);
        if (written < count) {
            for (size_t i = written; i < count; ++i) {
                out[i] = ' ';
            }
        }
    }
}

static void mezcomp_format_apps_bar(char* out, size_t count) {
    if (!out || count == 0) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = ' ';
    }
    static const char* kLabel = "apps: [placeholder]";
    size_t start = 2;
    if (start >= count) {
        start = 0;
    }
    size_t avail = count - start;
    size_t label_len = strlen(kLabel);
    if (label_len > avail) {
        label_len = avail;
    }
    for (size_t i = 0; i < label_len; ++i) {
        out[start + i] = kLabel[i];
    }
}

static void mezcomp_fb_draw_glyph(const mezcomp_fb_ctx_t* ctx, int x, int y, uint8_t fg, uint8_t bg, const mezcomp_glyph_t* glyph) {
    if (!ctx || !glyph) return;
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = glyph->rows[row];
        for (int col = 0; col < 6; ++col) {
            uint8_t mask = (uint8_t)(1u << (5 - col));
            uint8_t color = (bits & mask) ? fg : bg;
            mezcomp_fb_fill_rect(ctx, x + col, y + row, 1, 1, color);
        }
    }
}

static void mezcomp_fb_draw_text(const mezcomp_fb_ctx_t* ctx, int x, int y, uint8_t fg, uint8_t bg, const char* text) {
    if (!ctx || !text) return;
    int pen_x = x;
    for (const char* p = text; *p; ++p) {
        const mezcomp_glyph_t* glyph = mezcomp_find_glyph(*p);
        mezcomp_fb_draw_glyph(ctx, pen_x, y, fg, bg, glyph);
        pen_x += 6;
        mezcomp_fb_fill_rect(ctx, pen_x, y, 1, 7, bg);
        ++pen_x;
    }
}

static void mezcomp_fb_draw_text_span(const mezcomp_fb_ctx_t* ctx, int x, int y, uint8_t fg, uint8_t bg, const char* text, size_t len) {
    if (!ctx || !text) return;
    int pen_x = x;
    for (size_t i = 0; i < len; ++i) {
        const mezcomp_glyph_t* glyph = mezcomp_find_glyph(text[i]);
        mezcomp_fb_draw_glyph(ctx, pen_x, y, fg, bg, glyph);
        pen_x += 6;
        mezcomp_fb_fill_rect(ctx, pen_x, y, 1, 7, bg);
        ++pen_x;
    }
}

static void mezcomp_fb_draw_bar(const mezcomp_fb_ctx_t* ctx, int y, uint8_t bg, uint8_t fg, const char* text, size_t len) {
    if (!ctx || !ctx->has_fb) return;
    int bar_height = 16;
    if (y < 0) {
        y = 0;
    }
    if (y >= ctx->height) {
        return;
    }
    if (y + bar_height > ctx->height) {
        bar_height = ctx->height - y;
    }
    if (bar_height <= 0) {
        return;
    }
    mezcomp_fb_fill_rect(ctx, 0, y, ctx->width, bar_height, bg);
    if (!text || len == 0) return;
    int text_y = y + 4;
    if (text_y + 7 > ctx->height) {
        text_y = ctx->height - 8;
    }
    mezcomp_fb_draw_text_span(ctx, 8, text_y, fg, bg, text, len);
}

static void mezcomp_fb_draw_window(const mezcomp_fb_ctx_t* ctx, const mezcomp_window_t* win, bool focused) {
    if (!ctx || !win) return;
    const int char_w = 8;
    const int char_h = 16;

    int px = win->x * char_w;
    int py = win->y * char_h;
    int pw = win->w * char_w;
    int ph = win->h * char_h;

    uint8_t bg_body = focused ? 50 : 40;
    uint8_t border = focused ? 180 : 120;
    uint8_t title_bg = focused ? 200 : 150;
    uint8_t title_fg = focused ? 15 : 24;
    uint8_t body_fg = 20;

    mezcomp_fb_fill_rect(ctx, px, py, pw, ph, bg_body);
    mezcomp_fb_draw_border(ctx, px, py, pw, ph, border);

    int title_height = 14;
    if (title_height > ph - 2) title_height = ph - 2;
    if (title_height < 6) title_height = 6;
    mezcomp_fb_fill_rect(ctx, px + 1, py + 1, pw - 2, title_height, title_bg);
    mezcomp_fb_draw_text(ctx, px + 4, py + 3, title_fg, title_bg, win->title);

    int text_y = py + title_height + 4;
    for (uint8_t i = 0; i < win->line_count; ++i) {
        mezcomp_fb_draw_text(ctx, px + 6, text_y, body_fg, bg_body, win->lines[i]);
        text_y += 10;
    }
}

static void mezcomp_fb_render(const mezcomp_fb_ctx_t* ctx, uint8_t focus_index, const char* status_line, size_t status_len, const char* apps_line, size_t apps_len) {
    if (!ctx || !ctx->has_fb) return;
    mezcomp_fb_fill_rect(ctx, 0, 0, ctx->width, ctx->height, 16);
    mezcomp_fb_draw_bar(ctx, 0, 32, 15, status_line, status_len);
    int bar_height = 16;
    int bottom_y = ctx->height - bar_height;
    if (bottom_y < 0) {
        bottom_y = 0;
    }
    mezcomp_fb_draw_bar(ctx, bottom_y, 24, 14, apps_line, apps_len);
    const size_t window_count = sizeof(kMezcompWindows) / sizeof(kMezcompWindows[0]);
    for (size_t i = 0; i < window_count; ++i) {
        mezcomp_fb_draw_window(ctx, &kMezcompWindows[i], focus_index == i);
    }
}

static void mezcomp_text_clear(const mez_api32_t* api) {
    if (!api) return;
    if (api->text_fill_line) {
        for (int y = 0; y < MEZCOMP_TEXT_ROWS; ++y) {
            api->text_fill_line(y, ' ', 0x17);
        }
    } else if (api->console_clear) {
        api->console_clear();
    }
}

static void mezcomp_text_draw_bar(const mez_api32_t* api, int row, uint8_t attr, const char* text, size_t len) {
    if (!api || !api->text_put) return;
    if (row < 0 || row >= MEZCOMP_TEXT_ROWS) return;
    for (int x = 0; x < MEZCOMP_STATUS_COLS; ++x) {
        char ch = ' ';
        if (text && (size_t)x < len) {
            ch = text[x];
            if (ch == '\0') {
                ch = ' ';
            }
        }
        api->text_put(x, row, ch, attr);
    }
}

static void mezcomp_text_draw_window(const mez_api32_t* api, const mezcomp_window_t* win, bool focused) {
    if (!api || !win || !api->text_put) return;
    uint8_t border_attr = focused ? 0x1F : 0x17;
    uint8_t title_attr = focused ? 0x1F : 0x1E;
    uint8_t body_attr = 0x70;

    int x0 = win->x;
    int y0 = win->y;
    int x1 = x0 + win->w - 1;
    int y1 = y0 + win->h - 1;

    for (int x = x0; x <= x1; ++x) {
        api->text_put(x, y0, '-', border_attr);
        api->text_put(x, y1, '-', border_attr);
    }
    for (int y = y0; y <= y1; ++y) {
        api->text_put(x0, y, '|', border_attr);
        api->text_put(x1, y, '|', border_attr);
    }
    api->text_put(x0, y0, '+', border_attr);
    api->text_put(x1, y0, '+', border_attr);
    api->text_put(x0, y1, '+', border_attr);
    api->text_put(x1, y1, '+', border_attr);

    int title_w = win->w - 2;
    int title_x = x0 + 1;
    for (int i = 0; i < title_w; ++i) {
        char ch = ' ';
        if (win->title[i] != '\0') {
            ch = win->title[i];
        }
        api->text_put(title_x + i, y0 + 1, ch, title_attr);
    }

    for (int y = y0 + 2; y < y1; ++y) {
        for (int x = x0 + 1; x < x1; ++x) {
            api->text_put(x, y, ' ', body_attr);
        }
    }

    int line_y = y0 + 3;
    for (uint8_t i = 0; i < win->line_count && line_y < y1; ++i, ++line_y) {
        const char* line = win->lines[i];
        int len = (int)strlen(line);
        if (len > win->w - 2) len = win->w - 2;
        for (int j = 0; j < len; ++j) {
            api->text_put(x0 + 1 + j, line_y, line[j], body_attr);
        }
    }
}

static void mezcomp_text_render(const mez_api32_t* api, uint8_t focus_index, const char* status_line, size_t status_len, const char* apps_line, size_t apps_len) {
    if (!api) return;
    mezcomp_text_clear(api);
    mezcomp_text_draw_bar(api, 0, 0x1F, status_line, status_len);
    int bottom_row = (MEZCOMP_TEXT_ROWS > 0) ? (MEZCOMP_TEXT_ROWS - 1) : 0;
    mezcomp_text_draw_bar(api, bottom_row, 0x1E, apps_line, apps_len);
    const size_t window_count = sizeof(kMezcompWindows) / sizeof(kMezcompWindows[0]);
    for (size_t i = 0; i < window_count; ++i) {
        mezcomp_text_draw_window(api, &kMezcompWindows[i], focus_index == i);
    }
}

static void mezcomp_render(const mez_api32_t* api, const mezcomp_fb_ctx_t* fb_ctx, bool has_fb, uint8_t focus_index) {
    char status_line[MEZCOMP_STATUS_COLS];
    char apps_line[MEZCOMP_STATUS_COLS];
    mezcomp_status_snapshot(api, status_line, MEZCOMP_STATUS_COLS);
    mezcomp_format_apps_bar(apps_line, MEZCOMP_STATUS_COLS);
    mezcomp_text_render(api, focus_index, status_line, MEZCOMP_STATUS_COLS, apps_line, MEZCOMP_STATUS_COLS);
    if (has_fb && fb_ctx) {
        mezcomp_fb_render(fb_ctx, focus_index, status_line, MEZCOMP_STATUS_COLS, apps_line, MEZCOMP_STATUS_COLS);
    }
}

static void mezcomp_update_hint_slot(const mez_api32_t* api, mez_status_slot_t slot, uint8_t focus_index) {
    if (!api || slot == MEZ_STATUS_SLOT_INVALID || !api->status_update) {
        return;
    }
    const size_t window_count = sizeof(kMezcompWindows) / sizeof(kMezcompWindows[0]);
    if (focus_index >= window_count) {
        return;
    }
    const mezcomp_window_t* win = &kMezcompWindows[focus_index];
    const char* prefix = "mezcompose: ";
    const char* suffix = " (Esc/Ctrl+Q)";
    char buffer[64];
    size_t pos = 0;
    for (const char* p = prefix; *p && pos + 1 < sizeof(buffer); ++p) {
        buffer[pos++] = *p;
    }
    for (const char* p = win->title; *p && pos + 1 < sizeof(buffer); ++p) {
        buffer[pos++] = *p;
    }
    for (const char* p = suffix; *p && pos + 1 < sizeof(buffer); ++p) {
        buffer[pos++] = *p;
    }
    buffer[pos] = '\0';
    api->status_update(slot, buffer);
}

static uint32_t mezcomp_time_hz(const mez_api32_t* api) {
    if (api && api->time_timer_hz) {
        uint32_t hz = api->time_timer_hz();
        if (hz) return hz;
    }
    return 100;
}

static void mezcomp_sleep(const mez_api32_t* api, uint32_t ms) {
    if (api && api->time_sleep_ms) {
        api->time_sleep_ms(ms);
        return;
    }
    volatile uint32_t spin = ms * 1000u;
    while (spin--) {
        (void)spin;
    }
}

int mezcompose_app_main(const mez_api32_t* api) {
    if (!api) return -1;
    if (api->console_writeln) {
        api->console_writeln("mezcompose: starting preview window manager");
    }

    mezcomp_fb_ctx_t fb_ctx;
    bool has_fb = mezcomp_fb_prepare(api, &fb_ctx);
    if (!has_fb && api->console_writeln) {
        api->console_writeln("mezcompose: framebuffer unavailable, text-mode only");
    }

    uint8_t focus = 0;
    const size_t window_count = sizeof(kMezcompWindows) / sizeof(kMezcompWindows[0]);
    uint32_t hz = mezcomp_time_hz(api);
    uint32_t ticks_per_swap = hz / 2;
    if (ticks_per_swap == 0) ticks_per_swap = 1;
    bool have_ticks = (api && api->time_ticks_get);
    uint32_t last_tick = have_ticks ? api->time_ticks_get() : 0;
    uint32_t fallback_ms = 0;

    mez_status_slot_t hint_slot = MEZ_STATUS_SLOT_INVALID;
    if (api->status_register && api->status_update) {
        hint_slot = api->status_register(MEZ_STATUS_POS_CENTER, 1, 0, 0, "mezcompose");
        if (hint_slot != MEZ_STATUS_SLOT_INVALID) {
            mezcomp_update_hint_slot(api, hint_slot, focus);
        }
    }

    mezcomp_render(api, has_fb ? &fb_ctx : NULL, has_fb, focus);

    while (1) {
        if (api->input_poll_key) {
            int key = api->input_poll_key();
            if (key == 27 || key == 'q' || key == 'Q' || key == 17) {
                break;
            }
        }

        bool do_swap = false;
        if (have_ticks) {
            uint32_t current_tick = api->time_ticks_get();
            uint32_t elapsed = current_tick - last_tick;
            if (elapsed >= ticks_per_swap) {
                last_tick = current_tick;
                do_swap = true;
            }
        } else {
            fallback_ms += 50;
            if (fallback_ms >= 500) {
                fallback_ms = 0;
                do_swap = true;
            }
        }
        if (do_swap) {
            focus = (uint8_t)((focus + 1) % window_count);
            mezcomp_update_hint_slot(api, hint_slot, focus);
            mezcomp_render(api, has_fb ? &fb_ctx : NULL, has_fb, focus);
        }

        mezcomp_sleep(api, 50);
    }

    if (hint_slot != MEZ_STATUS_SLOT_INVALID && api->status_update) {
        api->status_update(hint_slot, "mezcompose: exiting");
    }
    if (hint_slot != MEZ_STATUS_SLOT_INVALID && api->status_release) {
        api->status_release(hint_slot);
    }
    if (api->console_writeln) {
        api->console_writeln("mezcompose: bye");
    }
    return 0;
}

#else

int mezcompose_app_main(const mez_api32_t* api) {
    if (api && api->console_writeln) {
        api->console_writeln("mezcompose: disabled via CONFIG_MEZCOMPOSE_ENABLE");
    }
    return -1;
}

#endif
