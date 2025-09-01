#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char* s);
void console_writeln(const char* s);
void console_write_hex16(uint16_t v);
void console_write_dec(uint32_t v);
void console_draw_status_right(const char* buf, int len);

#endif // CONSOLE_H

