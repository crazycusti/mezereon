#include "main.h"
#include "config.h"
#include <stdint.h>
#include "arch/x86/io.h"

#define VIDEO_MEMORY 0xb8000
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static int vga_row = 0;
static int vga_col = 0;
static const char HEX_DIGITS[] = "0123456789ABCDEF";

/* Forward declarations to avoid implicit declaration warnings */
void video_print(const char* str);
void video_println(const char* str);

static inline void vga_hw_cursor_update_internal(void) {
#if CONFIG_VIDEO_HW_CURSOR
    uint16_t pos = (uint16_t)(vga_row * CONFIG_VGA_WIDTH + vga_col);
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
#endif
}

void video_update_cursor(void) {
    vga_hw_cursor_update_internal();
}

static inline void video_scroll(void) {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    const int W = CONFIG_VGA_WIDTH;
    const int H = CONFIG_VGA_HEIGHT;
    // Move lines 1..H-1 up to 0..H-2
    for (int y = 1; y < H; y++) {
        for (int x = 0; x < W; x++) {
            video[(y - 1) * W + x] = video[y * W + x];
        }
    }
    // Clear last line
    for (int x = 0; x < W; x++) {
        video[(H - 1) * W + x] = (0x07 << 8) | ' ';
    }
    vga_row = H - 1;
    if (vga_col >= W) vga_col = 0;
    vga_hw_cursor_update_internal();
}

// --- Simple status bar state (top row, blue background) ---
static char status_left[128];
static int  status_left_len = 0;
static char status_right[64];
static int  status_right_len = 0;

static inline void video_status_redraw(void) {
    volatile uint16_t* vga = (volatile uint16_t*)VIDEO_MEMORY;
    const uint16_t blue_sp = (uint16_t)((0x1F << 8) | ' ');
    // Fill entire top row with blue background
    for (int x=0; x<CONFIG_VGA_WIDTH; x++) vga[x] = blue_sp;
    // Draw left text
    int ll = status_left_len; if (ll < 0) ll = 0; if (ll > CONFIG_VGA_WIDTH) ll = CONFIG_VGA_WIDTH;
    for (int i=0; i<ll && i<CONFIG_VGA_WIDTH; i++) vga[i] = (uint16_t)((0x1F << 8) | (uint8_t)status_left[i]);
    // Draw right text (right-aligned)
    int rl = status_right_len; if (rl < 0) rl = 0; if (rl > CONFIG_VGA_WIDTH) rl = CONFIG_VGA_WIDTH;
    int start = CONFIG_VGA_WIDTH - rl; if (start < 0) start = 0;
    for (int i=0; i<rl && (start + i) < CONFIG_VGA_WIDTH; i++) vga[start + i] = (uint16_t)((0x1F << 8) | (uint8_t)status_right[i]);
}

void video_status_set_right(const char* buf, int len) {
    if (!buf || len<=0) { status_right_len = 0; status_right[0] = 0; video_status_redraw(); return; }
    if (len > (int)sizeof(status_right)) len = (int)sizeof(status_right);
    for (int i=0;i<len;i++) status_right[i] = buf[i];
    status_right_len = len;
    if (status_right_len > 0 && status_right[status_right_len-1] == '\0') status_right_len--; // ignore trailing NUL in len
    video_status_redraw();
}

void video_status_set_left(const char* buf, int len) {
    if (!buf || len<=0) { status_left_len = 0; status_left[0] = 0; video_status_redraw(); return; }
    if (len > (int)sizeof(status_left)) len = (int)sizeof(status_left);
    for (int i=0;i<len;i++) status_left[i] = buf[i];
    status_left_len = len;
    if (status_left_len > 0 && status_left[status_left_len-1] == '\0') status_left_len--; // ignore trailing NUL in len
    video_status_redraw();
}

void video_init() {
#if CONFIG_VIDEO_CLEAR_ON_INIT
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    for (int y = 0; y < CONFIG_VGA_HEIGHT; y++) {
        for (int x = 0; x < CONFIG_VGA_WIDTH; x++) {
            video[y * CONFIG_VGA_WIDTH + x] = (0x07 << 8) | ' ';
        }
    }
    vga_row = 0;
    vga_col = 0;
    vga_hw_cursor_update_internal();
#else
    /*
     * Preserve bootloader output: do NOT clear the screen.
     * Read current hardware cursor to continue printing after BIOS text.
     */
    /* Cursor high */
    outb(VGA_CRTC_INDEX, 0x0E);
    uint16_t pos = ((uint16_t)inb(VGA_CRTC_DATA)) << 8;
    /* Cursor low */
    outb(VGA_CRTC_INDEX, 0x0F);
    pos |= inb(VGA_CRTC_DATA);

    if (pos >= (CONFIG_VGA_WIDTH * CONFIG_VGA_HEIGHT)) {
        vga_row = 0;
        vga_col = 0;
    } else {
        vga_row = pos / CONFIG_VGA_WIDTH;
        vga_col = pos % CONFIG_VGA_WIDTH;
        /* Optional: move to next line to avoid overwriting the last char */
        if (vga_col != 0) {
            vga_row++;
            vga_col = 0;
        }
        if (vga_row >= CONFIG_VGA_HEIGHT) {
            vga_row = CONFIG_VGA_HEIGHT - 1;
            vga_col = 0;
        }
    }
    vga_hw_cursor_update_internal();
#endif
}

void video_clear() {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    for (int y = 0; y < CONFIG_VGA_HEIGHT; y++) {
        for (int x = 0; x < CONFIG_VGA_WIDTH; x++) {
            video[y * CONFIG_VGA_WIDTH + x] = (0x07 << 8) | ' ';
        }
    }
    vga_row = 0;
    vga_col = 0;
    vga_hw_cursor_update_internal();
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

void video_print_dec(uint32_t v) {
    char buf[11]; // up to 4294967295
    int i = 0;
    if (v == 0) { video_print("0"); return; }
    while (v > 0 && i < 10) {
        uint32_t q = v / 10;
        uint32_t r = v - q * 10;
        buf[i++] = (char)('0' + r);
        v = q;
    }
    // reverse
    for (int j = i - 1; j >= 0; j--) {
        char s[2]; s[0] = buf[j]; s[1] = 0; video_print(s);
    }
}

void video_print(const char* str) {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    while (*str) {
        if (*str == '\n') {
            vga_row++;
            vga_col = 0;
            if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
        } else {
            if (vga_col >= CONFIG_VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
                if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
            }
            if (vga_row < CONFIG_VGA_HEIGHT) {
                video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | *str;
            }
            vga_col++;
            if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
        }
        str++;
    }
    if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
    if (vga_col >= CONFIG_VGA_WIDTH) { vga_col = CONFIG_VGA_WIDTH - 1; }
    vga_hw_cursor_update_internal();
}

void video_println(const char* str) {
    video_print(str);
    video_print("\n");
}

void video_putc(char c) {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    if (c == '\n') {
        vga_row++; vga_col = 0;
        if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
        vga_hw_cursor_update_internal();
        return;
    }
    if (c == '\r') { vga_col = 0; vga_hw_cursor_update_internal(); return; }
    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | ' ';
        }
        vga_hw_cursor_update_internal();
        return;
    }
    if (vga_col >= CONFIG_VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
    if (vga_row < CONFIG_VGA_HEIGHT) {
        video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | (uint8_t)c;
    }
    vga_col++;
    if (vga_row >= CONFIG_VGA_HEIGHT) video_scroll();
    if (vga_col >= CONFIG_VGA_WIDTH) { vga_col = CONFIG_VGA_WIDTH - 1; }
    vga_hw_cursor_update_internal();
}
