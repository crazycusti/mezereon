#include <stdint.h>
#include <stddef.h>
#include "console_backend.h"

// Framebuffer/firmware console stub backend.
// This is a placeholder for non-VGA platforms; it currently discards output.

void cback_init(void) {}
void cback_clear(void) {}
void cback_putc(char c) { (void)c; }
void cback_write(const char* s) { (void)s; }
void cback_writeln(const char* s) { (void)s; }
void cback_write_hex16(uint16_t v) { (void)v; }
void cback_write_hex32(uint32_t v) { (void)v; }
void cback_write_dec(uint32_t v) { (void)v; }
void cback_status_draw_full(const char* text, int len) { (void)text; (void)len; }
int cback_fb_active(void) { return 0; }
const void* cback_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    (void)pitch; (void)width; (void)height; (void)bpp;
    return NULL;
}
