#include "main.h"
#include "config.h"
#include <stdint.h>

#define VIDEO_MEMORY 0xb8000
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int vga_row = 0;
static int vga_col = 0;
static const char HEX_DIGITS[] = "0123456789ABCDEF";

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

void video_print(const char* str) {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    while (*str) {
        if (*str == '\n') {
            vga_row++;
            vga_col = 0;
        } else {
            if (vga_col >= CONFIG_VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
            }
            if (vga_row < CONFIG_VGA_HEIGHT) {
                video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | *str;
            }
            vga_col++;
        }
        str++;
    }
    if (vga_row >= CONFIG_VGA_HEIGHT) { vga_row = CONFIG_VGA_HEIGHT - 1; }
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
        vga_row++; vga_col = 0; vga_hw_cursor_update_internal(); return;
    }
    if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | ' ';
        }
        vga_hw_cursor_update_internal();
        return;
    }
    if (vga_col >= CONFIG_VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row < CONFIG_VGA_HEIGHT) {
        video[vga_row * CONFIG_VGA_WIDTH + vga_col] = (0x07 << 8) | (uint8_t)c;
    }
    vga_col++;
    if (vga_row >= CONFIG_VGA_HEIGHT) { vga_row = CONFIG_VGA_HEIGHT - 1; }
    if (vga_col >= CONFIG_VGA_WIDTH) { vga_col = CONFIG_VGA_WIDTH - 1; }
    vga_hw_cursor_update_internal();
}
