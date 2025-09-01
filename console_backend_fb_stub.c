#include <stdint.h>
#include "console_backend.h"

// Framebuffer/firmware console stub backend.
// This is a placeholder for non-VGA platforms; it currently discards output.

void cback_init(void) {}
void cback_clear(void) {}
void cback_putc(char c) { (void)c; }
void cback_write(const char* s) { (void)s; }
void cback_writeln(const char* s) { (void)s; }
void cback_write_hex16(uint16_t v) { (void)v; }
void cback_write_dec(uint32_t v) { (void)v; }
void cback_draw_status_right(const char* buf, int len) { (void)buf; (void)len; }

