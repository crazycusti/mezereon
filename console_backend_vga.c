#include <stdint.h>

// VGA textmode backend mapping to video.c
extern void video_init();
extern void video_clear();
extern void video_print(const char* str);
extern void video_println(const char* str);
extern void video_print_hex16(uint16_t v);
extern void video_print_dec(uint32_t v);
extern void video_putc(char c);
extern void video_draw_status_right(const char* buf, int len);

#include "console_backend.h"

void cback_init(void)                  { video_init(); }
void cback_clear(void)                 { video_clear(); }
void cback_putc(char c)                { video_putc(c); }
void cback_write(const char* s)        { video_print(s); }
void cback_writeln(const char* s)      { video_println(s); }
void cback_write_hex16(uint16_t v)     { video_print_hex16(v); }
void cback_write_dec(uint32_t v)       { video_print_dec(v); }
void cback_draw_status_right(const char* buf, int len) { video_draw_status_right(buf, len); }

