#include "console.h"

// Backend: current implementation uses ISA VGA textmode from video.c
extern void video_init();
extern void video_clear();
extern void video_print(const char* str);
extern void video_println(const char* str);
extern void video_print_hex16(uint16_t v);
extern void video_print_dec(uint32_t v);
extern void video_putc(char c);
extern void video_draw_status_right(const char* buf, int len);

void console_init(void)                  { video_init(); }
void console_clear(void)                 { video_clear(); }
void console_putc(char c)                { video_putc(c); }
void console_write(const char* s)        { video_print(s); }
void console_writeln(const char* s)      { video_println(s); }
void console_write_hex16(uint16_t v)     { video_print_hex16(v); }
void console_write_dec(uint32_t v)       { video_print_dec(v); }
void console_draw_status_right(const char* buf, int len) { video_draw_status_right(buf, len); }

