#include "console.h"
#include "console_backend.h"

void console_init(void)                  { cback_init(); }
void console_clear(void)                 { cback_clear(); }
void console_putc(char c)                { cback_putc(c); }
void console_write(const char* s)        { cback_write(s); }
void console_writeln(const char* s)      { cback_writeln(s); }
void console_write_hex16(uint16_t v)     { cback_write_hex16(v); }
void console_write_dec(uint32_t v)       { cback_write_dec(v); }
void console_draw_status_right(const char* buf, int len) { cback_draw_status_right(buf, len); }
void console_status_set_left(const char* s) {
    if (!s) { cback_status_set_left("", 0); return; }
    int len = 0; while (s[len] && len < 255) len++;
    cback_status_set_left(s, len);
}
