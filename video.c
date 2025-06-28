#include "main.h"
#include "config.h"
#include <stdint.h>

#define VIDEO_MEMORY 0xb8000

void video()
{
    const char video_adress = (char)0xb8000; 
    unsigned int displaysize = 80*25*2;
    video_adress = 0x07; 
}

void video_init() {
    volatile uint16_t* video = (uint16_t*) VIDEO_MEMORY;
    for (int y = 0; y < CONFIG_VGA_HEIGHT; y++) {
        for (int x = 0; x < CONFIG_VGA_WIDTH; x++) {
            video[y * CONFIG_VGA_WIDTH + x] = (0x07 << 8) | ' ';
        }
    }
}
