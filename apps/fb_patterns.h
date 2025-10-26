#ifndef APPS_FB_PATTERNS_H
#define APPS_FB_PATTERNS_H

#include <stdint.h>

void fb_patterns_configure_palette(void);
void fb_patterns_draw_demo(volatile uint8_t* fb,
                           uint16_t width,
                           uint16_t height,
                           uint32_t pitch,
                           uint8_t bpp);

#endif /* APPS_FB_PATTERNS_H */
