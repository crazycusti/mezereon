#ifndef CONSOLE_BACKEND_H
#define CONSOLE_BACKEND_H

#include <stdint.h>

void cback_init(void);
void cback_clear(void);
void cback_putc(char c);
void cback_write(const char* s);
void cback_writeln(const char* s);
void cback_write_hex16(uint16_t v);
void cback_write_hex32(uint32_t v);
void cback_write_dec(uint32_t v);
void cback_draw_status_right(const char* buf, int len);
void cback_status_set_left(const char* buf, int len);

int cback_fb_active(void);
const void* cback_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp);

#endif // CONSOLE_BACKEND_H
