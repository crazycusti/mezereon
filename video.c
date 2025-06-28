#include "main.h"
#include <stdint.h>

#define VIDEO_MEMORY 0xb8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void video()
{
    const char video_adress = (char)0xb8000; 
    unsigned int displaysize = 80*25*2;
    video_adress = 0x07; 
}

void video_init() {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            video[y * VGA_WIDTH + x] = (0x07 << 8) | ' ';
        }
    }
}
