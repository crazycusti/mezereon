#ifndef VIDEO_FB_H
#define VIDEO_FB_H

#include "display.h"

void video_switch_to_framebuffer(const display_mode_info_t* mode);
void video_switch_to_text(void);

#endif // VIDEO_FB_H
