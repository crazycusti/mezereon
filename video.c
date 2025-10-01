#include "main.h"
#include "config.h"
#include "display.h"
#include "video_fb.h"
#include "drivers/gpu/vga_hw.h"
#include "drivers/gpu/fb_accel.h"
#include "fonts/font8x16.h"
#include "platform.h"
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/io.h"

#define VIDEO_MEMORY 0xB8000
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

#define TEXT_COLS CONFIG_VGA_WIDTH
#define TEXT_ROWS CONFIG_VGA_HEIGHT
#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16

typedef struct {
    uint8_t ch;
    uint8_t attr;
} text_cell_t;

static text_cell_t g_cells[TEXT_ROWS][TEXT_COLS];

static enum {
    VIDEO_TARGET_TEXT = 0,
    VIDEO_TARGET_FB = 1,
} g_target = VIDEO_TARGET_TEXT;

static volatile uint8_t* g_fb_ptr = NULL;
static uint32_t g_fb_pitch = 0;
static uint16_t g_fb_width = 0;
static uint16_t g_fb_height = 0;
static uint8_t g_fb_bpp = 0;

static int g_row = 1;
static int g_col = 0;

static const char HEX_DIGITS[] = "0123456789ABCDEF";

// --- Status bar state ---
static char status_left[128];
static int  status_left_len = 0;
static char status_right[64];
static int  status_right_len = 0;
static uint8_t g_cursor_fb_visible = 1;
static uint32_t g_cursor_fb_last_toggle = 0;
static void video_cursor_refresh_fb(void);
static void video_cursor_hide_current_fb(void);

void video_print(const char* str);

static inline uint8_t attr_fg(uint8_t attr) {
    return (uint8_t)((attr & 0x0F) & 0x0F);
}

static inline uint8_t attr_bg(uint8_t attr) {
    uint8_t bg = (uint8_t)((attr >> 4) & 0x07);
    if (attr & 0x80) bg |= 0x08;
    return bg;
}

static inline void vga_hw_cursor_update_internal(void) {
#if CONFIG_VIDEO_HW_CURSOR
    int row = g_row;
    if (row < 0) row = 0;
    if (row >= TEXT_ROWS) row = TEXT_ROWS - 1;
    int col = g_col;
    if (col < 0) col = 0;
    if (col >= TEXT_COLS) col = TEXT_COLS - 1;
    uint16_t pos = (uint16_t)(row * TEXT_COLS + col);
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
#endif
}

void video_update_cursor(void) {
    vga_hw_cursor_update_internal();
}

static inline void video_write_text_cell_direct(int row, int col, const text_cell_t* cell) {
    volatile uint16_t* video = (uint16_t*)VIDEO_MEMORY;
    uint16_t value = (uint16_t)((uint16_t)cell->attr << 8) | (uint8_t)cell->ch;
    video[row * TEXT_COLS + col] = value;
}

static void video_draw_cell_fb(int row, int col, const text_cell_t* cell) {
    if (g_target != VIDEO_TARGET_FB || !g_fb_ptr || g_fb_bpp != 8) return;
    int px = col * CHAR_WIDTH;
    int py = row * CHAR_HEIGHT;
    if (px + CHAR_WIDTH > g_fb_width) return;
    if (py + CHAR_HEIGHT > g_fb_height) return;

    uint8_t fg = attr_fg(cell->attr);
    uint8_t bg = attr_bg(cell->attr);
    const uint8_t* glyph = font8x16_get(cell->ch);
    int is_cursor = (row == g_row && col == g_col);
    int gradient_row = (row == 0);

    int accelerated = (!gradient_row && fb_accel_available());
    if (accelerated) {
        if (!fb_accel_fill_rect((uint16_t)px, (uint16_t)py, CHAR_WIDTH, CHAR_HEIGHT, bg)) {
            accelerated = 0;
        }
    }

    for (int y = 0; y < CHAR_HEIGHT; y++) {
        uint8_t bits = glyph[y];
        volatile uint8_t* line = g_fb_ptr + (py + y) * g_fb_pitch + px;
        for (int x = 0; x < CHAR_WIDTH; x++) {
            uint8_t mask = (uint8_t)(0x80u >> x);
            uint8_t color = bg;
            int write_pixel = 0;

            if (gradient_row) {
                uint32_t denom = (g_fb_width > 1) ? (g_fb_width - 1) : 1;
                uint32_t step = ((uint32_t)(px + x) * 16u) / denom;
                color = (uint8_t)(240 + (step & 0x0F));
                if (bits & mask) color = 15;
                write_pixel = 1;
            } else {
                if (bits & mask) {
                    color = fg;
                    write_pixel = 1;
                }
            }
            if (is_cursor && g_cursor_fb_visible && y >= CHAR_HEIGHT - 2) {
                color = 15;
                write_pixel = 1;
            }
            if (!accelerated && !write_pixel) {
                color = bg;
                write_pixel = 1;
            }
            if (write_pixel) {
                line[x] = color;
            }
        }
    }
    fb_accel_mark_dirty((uint16_t)px, (uint16_t)py, CHAR_WIDTH, CHAR_HEIGHT);
    fb_accel_sync();
}

static void video_cursor_refresh_fb(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    if (g_row < 0 || g_row >= TEXT_ROWS || g_col < 0 || g_col >= TEXT_COLS) return;
    video_draw_cell_fb(g_row, g_col, &g_cells[g_row][g_col]);
}

static void video_cursor_hide_current_fb(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    if (g_row < 0 || g_row >= TEXT_ROWS || g_col < 0 || g_col >= TEXT_COLS) return;
    g_cursor_fb_visible = 0;
    video_draw_cell_fb(g_row, g_col, &g_cells[g_row][g_col]);
}

static void video_sync_cell(int row, int col) {
    const text_cell_t* cell = &g_cells[row][col];
    video_write_text_cell_direct(row, col, cell);
    if (g_target == VIDEO_TARGET_FB)
        video_draw_cell_fb(row, col, cell);
}

static void video_put_cell(int row, int col, char ch, uint8_t attr) {
    if (row < 0 || row >= TEXT_ROWS || col < 0 || col >= TEXT_COLS) return;
    text_cell_t* cell = &g_cells[row][col];
    cell->ch = (uint8_t)ch;
    cell->attr = attr;
    video_sync_cell(row, col);
}

static void video_redraw_range(int row_start, int row_end) {
    if (row_start < 0) row_start = 0;
    if (row_end > TEXT_ROWS) row_end = TEXT_ROWS;
    for (int y = row_start; y < row_end; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            video_sync_cell(y, x);
        }
    }
}

static void video_redraw_textmode(void) {
    for (int y = 0; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            video_write_text_cell_direct(y, x, &g_cells[y][x]);
        }
    }
}

static void video_redraw_framebuffer(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    for (int y = 0; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            video_draw_cell_fb(y, x, &g_cells[y][x]);
        }
    }
}

static void video_status_redraw(void) {
    for (int x = 0; x < TEXT_COLS; x++) {
        video_put_cell(0, x, ' ', 0x1F);
    }
    int ll = status_left_len;
    if (ll < 0) ll = 0;
    if (ll > TEXT_COLS) ll = TEXT_COLS;
    for (int i = 0; i < ll; i++) {
        video_put_cell(0, i, status_left[i], 0x1F);
    }
    const char* mode = (g_target == VIDEO_TARGET_FB) ? " | gfx: framebuffer" : " | gfx: text";
    int mode_len = 0;
    while (mode[mode_len]) mode_len++;
    int mode_pos = ll;
    int rl = status_right_len;
    if (rl < 0) rl = 0;
    if (rl > TEXT_COLS) rl = TEXT_COLS;
    int right_start = TEXT_COLS - rl;
    if (right_start < 0) right_start = 0;

    if (mode_pos < right_start) {
        int draw_len = mode_len;
        if (mode_pos + draw_len > right_start) draw_len = right_start - mode_pos;
        for (int i = 0; i < draw_len && (mode_pos + i) < TEXT_COLS; i++) {
            video_put_cell(0, mode_pos + i, mode[i], 0x1F);
        }
        if (mode_pos + draw_len > ll) ll = mode_pos + draw_len;
        if (ll > TEXT_COLS) ll = TEXT_COLS;
    }

    for (int i = 0; i < rl && (right_start + i) < TEXT_COLS; i++) {
        video_put_cell(0, right_start + i, status_right[i], 0x1F);
    }
}

static void video_scroll(void) {
    video_cursor_hide_current_fb();
    for (int y = 2; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            g_cells[y - 1][x] = g_cells[y][x];
        }
    }
    for (int x = 0; x < TEXT_COLS; x++) {
        g_cells[TEXT_ROWS - 1][x].ch = ' ';
        g_cells[TEXT_ROWS - 1][x].attr = 0x07;
    }
    video_redraw_range(1, TEXT_ROWS);
    video_status_redraw();
    g_row = TEXT_ROWS - 1;
    g_col = 0;
    vga_hw_cursor_update_internal();
    g_cursor_fb_visible = 1;
    video_cursor_refresh_fb();
}

void video_status_set_right(const char* buf, int len) {
    if (!buf || len <= 0) { status_right_len = 0; status_right[0] = 0; video_status_redraw(); return; }
    if (len > (int)sizeof(status_right)) len = (int)sizeof(status_right);
    for (int i = 0; i < len; i++) status_right[i] = buf[i];
    status_right_len = len;
    if (status_right_len > 0 && status_right[status_right_len - 1] == '\0') status_right_len--;
    video_status_redraw();
}

void video_status_set_left(const char* buf, int len) {
    if (!buf || len <= 0) { status_left_len = 0; status_left[0] = 0; video_status_redraw(); return; }
    if (len > (int)sizeof(status_left)) len = (int)sizeof(status_left);
    for (int i = 0; i < len; i++) status_left[i] = buf[i];
    status_left_len = len;
    if (status_left_len > 0 && status_left[status_left_len - 1] == '\0') status_left_len--;
    video_status_redraw();
}

void video_init() {
    g_target = VIDEO_TARGET_TEXT;
    g_fb_ptr = NULL;
    g_fb_pitch = 0;
    g_fb_width = 0;
    g_fb_height = 0;
    g_fb_bpp = 0;

    for (int y = 0; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            g_cells[y][x].ch = ' ';
            g_cells[y][x].attr = 0x07;
        }
    }

#if CONFIG_VIDEO_CLEAR_ON_INIT
    g_row = 1;
    g_col = 0;
    video_redraw_textmode();
#else
    volatile uint16_t* video = (uint16_t*)VIDEO_MEMORY;
    for (int y = 0; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            uint16_t val = video[y * TEXT_COLS + x];
            g_cells[y][x].ch = (uint8_t)(val & 0xFF);
            g_cells[y][x].attr = (uint8_t)(val >> 8);
        }
    }
    outb(VGA_CRTC_INDEX, 0x0E);
    uint16_t pos = ((uint16_t)inb(VGA_CRTC_DATA)) << 8;
    outb(VGA_CRTC_INDEX, 0x0F);
    pos |= inb(VGA_CRTC_DATA);
    if (pos >= (TEXT_COLS * TEXT_ROWS)) pos = TEXT_COLS;
    g_row = (pos / TEXT_COLS);
    g_col = (pos % TEXT_COLS);
    if (g_row < 1) g_row = 1;
#endif

    display_manager_set_text_mode("vga-text", TEXT_COLS, TEXT_ROWS, 16);
    video_status_redraw();
    video_redraw_textmode();
    vga_hw_cursor_update_internal();
}

void video_clear() {
    video_cursor_hide_current_fb();
    for (int y = 1; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            g_cells[y][x].ch = ' ';
            g_cells[y][x].attr = 0x07;
        }
    }
    g_row = 1;
    g_col = 0;
    video_redraw_range(1, TEXT_ROWS);
    video_status_redraw();
    vga_hw_cursor_update_internal();
    g_cursor_fb_visible = 1;
    video_cursor_refresh_fb();
}

void video_print_hex16(uint16_t v) {
    char buf[7];
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = HEX_DIGITS[(v >> 12) & 0xF];
    buf[3] = HEX_DIGITS[(v >> 8) & 0xF];
    buf[4] = HEX_DIGITS[(v >> 4) & 0xF];
    buf[5] = HEX_DIGITS[(v >> 0) & 0xF];
    buf[6] = '\0';
    video_print(buf);
}

void video_print_hex32(uint32_t v) {
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = HEX_DIGITS[(v >> (28 - i * 4)) & 0xF];
    buf[10] = '\0';
    video_print(buf);
}

void video_print_dec(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { video_print("0"); return; }
    while (v > 0 && i < 10) {
        uint32_t q = v / 10;
        uint32_t r = v - q * 10;
        buf[i++] = (char)('0' + r);
        v = q;
    }
    for (int j = i - 1; j >= 0; j--) {
        char s[2]; s[0] = buf[j]; s[1] = 0; video_print(s);
    }
}

void video_print(const char* str) {
    while (*str) {
        char c = *str++;
        video_cursor_hide_current_fb();
        if (c == '\n') {
            g_row++;
            g_col = 0;
            if (g_row >= TEXT_ROWS) video_scroll();
        } else {
            if (g_col >= TEXT_COLS) {
                g_col = 0;
                g_row++;
                if (g_row >= TEXT_ROWS) video_scroll();
            }
            if (g_row < TEXT_ROWS) {
                video_put_cell(g_row, g_col, c, 0x07);
                g_col++;
            }
        }
        g_cursor_fb_visible = 1;
        video_cursor_refresh_fb();
    }
    vga_hw_cursor_update_internal();
}

void video_println(const char* str) {
    video_print(str);
    video_print("\n");
}

void video_putc(char c) {
    video_cursor_hide_current_fb();
    if (c == '\n') {
        g_row++;
        g_col = 0;
        if (g_row >= TEXT_ROWS) video_scroll();
        vga_hw_cursor_update_internal();
        g_cursor_fb_visible = 1;
        video_cursor_refresh_fb();
        return;
    }
    if (c == '\r') {
        g_col = 0;
        vga_hw_cursor_update_internal();
        g_cursor_fb_visible = 1;
        video_cursor_refresh_fb();
        return;
    }
    if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            video_put_cell(g_row, g_col, ' ', 0x07);
        }
        vga_hw_cursor_update_internal();
        g_cursor_fb_visible = 1;
        video_cursor_refresh_fb();
        return;
    }
    if (g_col >= TEXT_COLS) {
        g_col = 0;
        g_row++;
        if (g_row >= TEXT_ROWS) video_scroll();
    }
    if (g_row < TEXT_ROWS) {
        video_put_cell(g_row, g_col, c, 0x07);
        g_col++;
    }
    vga_hw_cursor_update_internal();
    g_cursor_fb_visible = 1;
    video_cursor_refresh_fb();
}

void video_switch_to_framebuffer(const display_mode_info_t* mode) {
    if (!mode || mode->kind != DISPLAY_MODE_KIND_FRAMEBUFFER) return;
    if (!mode->framebuffer || mode->bpp != 8) return;

    g_fb_ptr = mode->framebuffer;
    g_fb_pitch = mode->pitch;
    g_fb_width = mode->width;
    g_fb_height = mode->height;
    g_fb_bpp = mode->bpp;
    g_target = VIDEO_TARGET_FB;

    vga_dac_load_default_palette();
    video_redraw_framebuffer();
    g_cursor_fb_visible = 1;
    g_cursor_fb_last_toggle = platform_ticks_get();
    video_cursor_refresh_fb();
}

void video_switch_to_text(void) {
    g_target = VIDEO_TARGET_TEXT;
    g_fb_ptr = NULL;
    g_fb_pitch = 0;
    g_fb_width = 0;
    g_fb_height = 0;
    g_fb_bpp = 0;

    fb_accel_reset();

    video_redraw_textmode();
    video_status_redraw();
    vga_hw_cursor_update_internal();
    g_cursor_fb_visible = 1;
}

void video_cursor_tick(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    uint32_t now = platform_ticks_get();
    if ((now - g_cursor_fb_last_toggle) >= 10u) {
        g_cursor_fb_last_toggle = now;
        g_cursor_fb_visible = (uint8_t)!g_cursor_fb_visible;
        video_cursor_refresh_fb();
    }
}

int video_fb_active(void) {
    return (g_target == VIDEO_TARGET_FB && g_fb_ptr != NULL);
}

const void* video_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    if (!video_fb_active()) return NULL;
    if (pitch) *pitch = g_fb_pitch;
    if (width) *width = g_fb_width;
    if (height) *height = g_fb_height;
    if (bpp) *bpp = g_fb_bpp;
    return (const void*)g_fb_ptr;
}
