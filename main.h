#include "config.h"
#include "netface.h"

void video_init();
void video_clear();
void video_print(const char* str);
void video_println(const char* str);
void video_print_hex16(unsigned short v);
void video_print_dec(unsigned int v);
void video_putc(char c);
void video_update_cursor(void);
void video_draw_status_right(const char* buf, int len);
