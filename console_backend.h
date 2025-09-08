#ifndef CONSOLE_BACKEND_H
#define CONSOLE_BACKEND_H

#include <stdint.h>

void cback_init(void);
void cback_clear(void);
void cback_putc(char c);
void cback_write(const char* s);
void cback_writeln(const char* s);
void cback_write_hex16(uint16_t v);
void cback_write_dec(uint32_t v);
void cback_draw_status_right(const char* buf, int len);
void cback_status_set_left(const char* buf, int len);

#endif // CONSOLE_BACKEND_H
