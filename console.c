#include "console.h"
#include "console_backend.h"
#include "statusbar.h"
#include "debug_serial.h"

void console_init(void)                  { cback_init(); statusbar_backend_ready(); }
void console_clear(void)                 { cback_clear(); }
void console_putc(char c)                { cback_putc(c); debug_serial_plugin_putc(c); }
void console_write(const char* s)        { cback_write(s); debug_serial_plugin_write(s); }
void console_writeln(const char* s)      { cback_writeln(s); debug_serial_plugin_writeln(s); }
void console_write_hex16(uint16_t v)     { cback_write_hex16(v); debug_serial_plugin_write_hex16(v); }
void console_write_hex32(uint32_t v)     { cback_write_hex32(v); debug_serial_plugin_write_hex32(v); }
void console_write_dec(uint32_t v)       { cback_write_dec(v); debug_serial_plugin_write_dec(v); }
void console_draw_status_right(const char* buf, int len) {
    if (!buf || len <= 0) {
        statusbar_legacy_set_right("");
        return;
    }
    if (len >= STATUSBAR_TEXT_MAX) len = STATUSBAR_TEXT_MAX - 1;
    char tmp[STATUSBAR_TEXT_MAX];
    for (int i = 0; i < len; i++) tmp[i] = buf[i];
    tmp[len] = '\0';
    statusbar_legacy_set_right(tmp);
}

void console_status_set_left(const char* s) {
    statusbar_legacy_set_left(s ? s : "");
}

void console_status_set_mid(const char* s) {
    statusbar_legacy_set_mid(s ? s : "");
}

void console_status_set_right(const char* s) {
    statusbar_legacy_set_right(s ? s : "");
}

int console_fb_active(void) {
    return cback_fb_active();
}

const void* console_fb_get_info(uint32_t* pitch, uint16_t* width, uint16_t* height, uint8_t* bpp) {
    return cback_fb_get_info(pitch, width, height, bpp);
}
