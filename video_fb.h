#ifndef VIDEO_FB_H
#define VIDEO_FB_H

#include "display.h"

void video_switch_to_framebuffer(const display_mode_info_t* mode);
void video_switch_to_text(void);
void video_cursor_tick(void);
int  video_fb_active(void);
const void* video_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp);

#endif // VIDEO_FB_H
