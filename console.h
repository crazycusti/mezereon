#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char* s);
void console_writeln(const char* s);
void console_write_hex16(uint16_t v);
void console_write_hex32(uint32_t v);
void console_write_dec(uint32_t v);
void console_draw_status_right(const char* buf, int len);
// Set left-aligned status text in the top status bar.
void console_status_set_left(const char* s);
void console_status_set_right(const char* s);

#endif // CONSOLE_H
