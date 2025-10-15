#include <stdint.h>

// VGA textmode backend mapping to video.c
extern void video_init();
extern void video_clear();
extern void video_print(const char* str);
extern void video_println(const char* str);
extern void video_print_hex16(uint16_t v);
extern void video_print_hex32(uint32_t v);
extern void video_print_dec(uint32_t v);
extern void video_putc(char c);
extern void video_status_draw_full(const char* text, int len);
extern int video_fb_active(void);
extern const void* video_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp);

#include "console_backend.h"

void cback_init(void)                  { video_init(); }
void cback_clear(void)                 { video_clear(); }
void cback_putc(char c)                { video_putc(c); }
void cback_write(const char* s)        { video_print(s); }
void cback_writeln(const char* s)      { video_println(s); }
void cback_write_hex16(uint16_t v)     { video_print_hex16(v); }
void cback_write_hex32(uint32_t v)     { video_print_hex32(v); }
void cback_write_dec(uint32_t v)       { video_print_dec(v); }
void cback_status_draw_full(const char* text, int len) { video_status_draw_full(text, len); }
int cback_fb_active(void) { return video_fb_active(); }
const void* cback_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    return video_fb_get_info(pitch, width, height, bpp);
}
