#include "main.h"
#include "config.h"
#include <stdint.h>

#define VIDEO_MEMORY 0xb8000

static int vga_row = 0;
static int vga_col = 0;

void video_init() {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    for (int y = 0; y < CONFIG_VGA_HEIGHT; y++) {
        for (int x = 0; x < CONFIG_VGA_WIDTH; x++) {
            video[y * CONFIG_VGA_WIDTH + x] = (0x07 << 8) | ' ';
        }
    }
    vga_row = 0;
    vga_col = 0;
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
}
