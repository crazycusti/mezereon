#include "main.h"
#include "config.h"
#include "display.h"
#include "video_fb.h"
#include "drivers/gpu/vga_hw.h"
#include "drivers/gpu/fb_accel.h"
#include "fonts/font8x16.h"
#include "platform.h"
#include "statusbar.h"
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
static int g_rows_current = TEXT_ROWS;
static int g_cols_current = TEXT_COLS;

static const char HEX_DIGITS[] = "0123456789ABCDEF";

// --- Status bar state ---
static char status_line[TEXT_COLS];
static uint8_t g_cursor_fb_visible = 1;
static uint32_t g_cursor_fb_last_toggle = 0;

static void video_draw_cell_fb(int row, int col, const text_cell_t* cell);
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
    if (row >= g_rows_current) row = g_rows_current - 1;
    int col = g_col;
    if (col < 0) col = 0;
    if (col >= g_cols_current) col = g_cols_current - 1;
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
    if (row >= 25 || col >= 80) return; // VGA limit
    volatile uint16_t* video = (uint16_t*)VIDEO_MEMORY;
    uint16_t value = (uint16_t)((uint16_t)cell->attr << 8) | (uint8_t)cell->ch;
    video[row * 80 + col] = value;
}

static inline void fb_write_pixel_4bpp(int x, int y, uint8_t color) {
    if (!g_fb_ptr) return;
    uint32_t offset = (uint32_t)y * g_fb_pitch + (uint32_t)(x >> 1);
    volatile uint8_t* cell = g_fb_ptr + offset;
    uint8_t current = *cell;
    if (x & 1) {
        current = (uint8_t)((current & 0xF0u) | (color & 0x0Fu));
    } else {
        current = (uint8_t)(((color & 0x0Fu) << 4) | (current & 0x0Fu));
    }
    *cell = current;
}

static void video_draw_cell_fb(int row, int col, const text_cell_t* cell) {
    if (g_target != VIDEO_TARGET_FB || !g_fb_ptr) return;
    int px = col * CHAR_WIDTH;
    int py = row * CHAR_HEIGHT;
    if (px + CHAR_WIDTH > g_fb_width) return;
    if (py + CHAR_HEIGHT > g_fb_height) return;

    uint8_t fg = attr_fg(cell->attr);
    uint8_t bg = attr_bg(cell->attr);
    const uint8_t* glyph = font8x16_get(cell->ch);
    int is_cursor = (row == g_row && col == g_col);
    int gradient_row = (row == 0);

    if (g_fb_bpp == 8) {
        for (int y = 0; y < CHAR_HEIGHT; y++) {
            uint8_t bits = glyph[y];
            volatile uint8_t* line = g_fb_ptr + (py + y) * g_fb_pitch + px;
            for (int x = 0; x < CHAR_WIDTH; x++) {
                uint8_t mask = (uint8_t)(0x80u >> x);
                uint8_t color = bg;
                if (gradient_row) {
                    uint32_t step = ((uint32_t)(px + x) * 16u) / g_fb_width;
                    color = (uint8_t)(240 + (step & 0x0F));
                    if (bits & mask) color = 15;
                } else {
                    if (bits & mask) color = fg;
                }
                if (is_cursor && g_cursor_fb_visible && y >= CHAR_HEIGHT - 2) color = 15;
                line[x] = color;
            }
        }
    } else { // 4 bpp
        for (int y = 0; y < CHAR_HEIGHT; y++) {
            uint8_t bits = glyph[y];
            for (int x = 0; x < CHAR_WIDTH; x++) {
                uint8_t mask = (uint8_t)(0x80u >> x);
                uint8_t color = bg & 0x0Fu;
                if (gradient_row) {
                    uint32_t step = ((uint32_t)(px + x) * 15u) / g_fb_width;
                    color = (uint8_t)(step & 0x0Fu);
                    if (bits & mask) color = fg & 0x0Fu;
                } else if (bits & mask) {
                    color = fg & 0x0Fu;
                }
                if (is_cursor && g_cursor_fb_visible && y >= CHAR_HEIGHT - 2) color = 0x0Fu;
                fb_write_pixel_4bpp(px + x, py + y, color);
            }
        }
    }
}

static void video_cursor_refresh_fb(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    if (g_row < 0 || g_row >= g_rows_current || g_col < 0 || g_col >= g_cols_current) return;
    video_draw_cell_fb(g_row, g_col, &g_cells[g_row][g_col]);
}

static void video_cursor_hide_current_fb(void) {
    if (g_target != VIDEO_TARGET_FB) return;
    if (g_row < 0 || g_row >= g_rows_current || g_col < 0 || g_col >= g_cols_current) return;
    g_cursor_fb_visible = 0;
    video_draw_cell_fb(g_row, g_col, &g_cells[g_row][g_col]);
}

static void video_sync_cell(int row, int col) {
    const text_cell_t* cell = &g_cells[row][col];
    if (g_target == VIDEO_TARGET_TEXT) {
        video_write_text_cell_direct(row, col, cell);
    } else {
        video_draw_cell_fb(row, col, cell);
    }
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
    if (row_end > g_rows_current) row_end = g_rows_current;
    for (int y = row_start; y < row_end; y++) {
        for (int x = 0; x < g_cols_current; x++) {
            video_sync_cell(y, x);
        }
    }
}

static void video_status_redraw(void) {
    for (int x = 0; x < g_cols_current; x++) {
        char ch = (x < TEXT_COLS) ? status_line[x] : ' ';
        if (ch == '\0') ch = ' ';
        video_put_cell(0, x, ch, 0x1F);
    }
}

void video_status_draw_full(const char* text, int len) {
    for (int i = 0; i < TEXT_COLS; i++) status_line[i] = ' ';
    if (text) {
        for (int i = 0; i < len && i < TEXT_COLS; i++) status_line[i] = text[i];
    }
    video_status_redraw();
}

static void video_scroll(void) {
    video_cursor_hide_current_fb();
    for (int y = 2; y < g_rows_current; y++) {
        for (int x = 0; x < g_cols_current; x++) {
            g_cells[y - 1][x] = g_cells[y][x];
        }
    }
    for (int x = 0; x < g_cols_current; x++) {
        g_cells[g_rows_current - 1][x].ch = ' ';
        g_cells[g_rows_current - 1][x].attr = 0x07;
    }
    video_redraw_range(1, g_rows_current);
    video_status_redraw();
    g_row = g_rows_current - 1;
    g_col = 0;
    vga_hw_cursor_update_internal();
    g_cursor_fb_visible = 1;
    video_cursor_refresh_fb();
}

void video_init() {
    g_target = VIDEO_TARGET_TEXT;
    g_fb_ptr = NULL;
    g_fb_width = 0;
    g_fb_height = 0;
    g_rows_current = 25;
    g_cols_current = 80;

    for (int y = 0; y < TEXT_ROWS; y++) {
        for (int x = 0; x < TEXT_COLS; x++) {
            g_cells[y][x].ch = ' ';
            g_cells[y][x].attr = 0x07;
        }
    }
    g_row = 1; g_col = 0;
    display_manager_set_text_mode("vga-text", 80, 25, 16);
    video_status_redraw();
}

void video_clear() {
    for (int y = 1; y < g_rows_current; y++) {
        for (int x = 0; x < g_cols_current; x++) {
            g_cells[y][x].ch = ' ';
            g_cells[y][x].attr = 0x07;
        }
    }
    g_row = 1; g_col = 0;
    video_redraw_range(1, g_rows_current);
}

void video_print(const char* str) {
    while (*str) {
        char c = *str++;
        video_cursor_hide_current_fb();
        if (c == '\n') {
            g_row++; g_col = 0;
            if (g_row >= g_rows_current) video_scroll();
        } else {
            if (g_col >= g_cols_current) {
                g_col = 0; g_row++;
                if (g_row >= g_rows_current) video_scroll();
            }
            if (g_row < g_rows_current) {
                video_put_cell(g_row, g_col, c, 0x07);
                g_col++;
            }
        }
        g_cursor_fb_visible = 1;
        video_cursor_refresh_fb();
    }
}

void video_println(const char* str) { video_print(str); video_print("\n"); }

void video_putc(char c) {
    char s[2] = {c, 0};
    video_print(s);
}

void video_print_hex16(uint16_t v) {
    char buf[7]; buf[0]='0'; buf[1]='x';
    for(int i=0;i<4;i++) buf[5-i]=HEX_DIGITS[(v>>(i*4))&0xF];
    buf[6]=0; video_print(buf);
}

void video_print_hex32(uint32_t v) {
    char buf[11]; buf[0]='0'; buf[1]='x';
    for(int i=0;i<8;i++) buf[9-i]=HEX_DIGITS[(v>>(i*4))&0xF];
    buf[10]=0; video_print(buf);
}

void video_print_dec(uint32_t v) {
    if(v==0){video_print("0");return;}
    char buf[11]; int i=0;
    while(v>0){ buf[i++]=(char)('0'+(v%10)); v/=10; }
    while(i>0){ char s[2]={buf[--i],0}; video_print(s); }
}

void video_switch_to_framebuffer(const display_mode_info_t* mode) {
    if (!mode || !mode->framebuffer) return;
    g_fb_ptr = mode->framebuffer;
    g_fb_pitch = mode->pitch;
    g_fb_width = mode->width;
    g_fb_height = mode->height;
    g_fb_bpp = mode->bpp;
    g_target = VIDEO_TARGET_FB;
    g_cols_current = g_fb_width / 8;
    g_rows_current = g_fb_height / 16;
    if (g_cols_current > TEXT_COLS) g_cols_current = TEXT_COLS;
    if (g_rows_current > TEXT_ROWS) g_rows_current = TEXT_ROWS;
    video_redraw_range(0, g_rows_current);
}

void video_switch_to_text(void) {
    g_target = VIDEO_TARGET_TEXT;
    g_rows_current = 25;
    g_cols_current = 80;
    fb_accel_reset();
    video_redraw_range(0, 25);
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

int video_fb_active(void) { return (g_target == VIDEO_TARGET_FB); }

const void* video_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    if (pitch) *pitch = g_fb_pitch;
    if (width) *width = g_fb_width;
    if (height) *height = g_fb_height;
    if (bpp) *bpp = g_fb_bpp;
    return (const void*)g_fb_ptr;
}

void video_status_draw_full_legacy(const char* text, int len) { video_status_draw_full(text, len); }
