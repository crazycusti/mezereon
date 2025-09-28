#ifndef FONTS_FONT8X16_H
#define FONTS_FONT8X16_H

#include <stdint.h>

extern const uint8_t g_font8x16[256 * 16];

static inline const uint8_t* font8x16_get(uint8_t ch)
{
    return &g_font8x16[(uint32_t)ch * 16u];
}

#endif // FONTS_FONT8X16_H
