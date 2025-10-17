#ifndef DEBUG_SERIAL_H
#define DEBUG_SERIAL_H

#include "config.h"
#include <stdint.h>

#if defined(CONFIG_DEBUG_SERIAL_PLUGIN) && (CONFIG_DEBUG_SERIAL_PLUGIN) && \
    defined(CONFIG_ARCH_X86) && (CONFIG_ARCH_X86)

#include "bootinfo.h"

void debug_serial_plugin_init(const boot_info_t* info);
void debug_serial_plugin_putc(char c);
void debug_serial_plugin_write(const char* s);
void debug_serial_plugin_writeln(const char* s);
void debug_serial_plugin_write_hex16(uint16_t v);
void debug_serial_plugin_write_hex32(uint32_t v);
void debug_serial_plugin_write_dec(uint32_t v);
void debug_serial_plugin_timer_tick(void);

#else

struct boot_info;
static inline void debug_serial_plugin_init(const struct boot_info* info) { (void)info; }
static inline void debug_serial_plugin_putc(char c) { (void)c; }
static inline void debug_serial_plugin_write(const char* s) { (void)s; }
static inline void debug_serial_plugin_writeln(const char* s) { (void)s; }
static inline void debug_serial_plugin_write_hex16(uint16_t v) { (void)v; }
static inline void debug_serial_plugin_write_hex32(uint32_t v) { (void)v; }
static inline void debug_serial_plugin_write_dec(uint32_t v) { (void)v; }
static inline void debug_serial_plugin_timer_tick(void) {}

#endif

#endif // DEBUG_SERIAL_H
