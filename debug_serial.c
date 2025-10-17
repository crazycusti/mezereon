#include "debug_serial.h"

#if defined(CONFIG_DEBUG_SERIAL_PLUGIN) && (CONFIG_DEBUG_SERIAL_PLUGIN) && \
    defined(CONFIG_ARCH_X86) && (CONFIG_ARCH_X86)

#include "arch/x86/io.h"
#include "config.h"
#include <stddef.h>

#define SERIAL_LSR_THRE 0x20u

static uint16_t g_serial_port = (uint16_t)CONFIG_DEBUG_SERIAL_PORT;
static int g_serial_ready = 0;
static uint32_t g_heartbeat_divider = 0;
static uint32_t g_heartbeat_sequence = 0;

static void serial_wait_transmit_empty(void) {
    while ((inb((uint16_t)(g_serial_port + 5u)) & SERIAL_LSR_THRE) == 0u) {
        /* busy wait */
    }
}

static void serial_write_char_raw(char c) {
    serial_wait_transmit_empty();
    outb(g_serial_port, (uint8_t)c);
}

static void serial_write_string_raw(const char* s) {
    if (!s) return;
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            serial_write_char_raw('\r');
        }
        serial_write_char_raw(c);
    }
}

static void serial_write_line_raw(const char* s) {
    serial_write_string_raw(s);
    serial_write_string_raw("\r\n");
}

static void serial_write_hex(uint32_t value, int digits) {
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    serial_write_string_raw("0x");
    for (int i = digits - 1; i >= 0; --i) {
        uint8_t nibble = (uint8_t)((value >> (i * 4)) & 0x0Fu);
        serial_write_char_raw(HEX_DIGITS[nibble]);
    }
}

static void serial_write_dec(uint32_t value) {
    char buf[11];
    int pos = 0;
    if (value == 0) {
        serial_write_char_raw('0');
        return;
    }
    while (value && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos--) {
        serial_write_char_raw(buf[pos]);
    }
}

static void serial_hw_init(void) {
    uint16_t port = g_serial_port;
    outb((uint16_t)(port + 1u), 0x00); // Disable interrupts
    outb((uint16_t)(port + 3u), 0x80); // Enable DLAB

    uint32_t divisor = 115200u / (uint32_t)CONFIG_DEBUG_SERIAL_BAUD;
    if (divisor == 0u) divisor = 1u;
    outb(port, (uint8_t)(divisor & 0xFFu));
    outb((uint16_t)(port + 1u), (uint8_t)((divisor >> 8) & 0xFFu));

    outb((uint16_t)(port + 3u), 0x03); // 8 data bits, 1 stop bit, no parity
    outb((uint16_t)(port + 2u), 0xC7); // Enable FIFO, clear
    outb((uint16_t)(port + 4u), 0x0B); // IRQs disabled, OUT2 set
}

static void debug_serial_log_bootinfo(const boot_info_t* info) {
    if (!info) {
        serial_write_line_raw("bootinfo: (null)");
        return;
    }

    serial_write_line_raw("bootinfo: available");

    serial_write_string_raw("  arch="); serial_write_dec(info->arch); serial_write_string_raw(" machine="); serial_write_dec(info->machine);
    serial_write_string_raw(" flags="); serial_write_hex(info->flags, 8); serial_write_string_raw("\r\n");

    serial_write_string_raw("  console=");
    if (info->console) serial_write_string_raw(info->console); else serial_write_string_raw("(null)");
    serial_write_string_raw(" boot_dev="); serial_write_dec(info->boot_device);
    serial_write_string_raw("\r\n");

    serial_write_string_raw("  vbe_mode="); serial_write_hex(info->vbe_mode, 4);
    serial_write_string_raw(" pitch="); serial_write_dec(info->vbe_pitch);
    serial_write_string_raw(" size="); serial_write_dec(info->vbe_width);
    serial_write_string_raw("x"); serial_write_dec(info->vbe_height);
    serial_write_string_raw("@"); serial_write_dec(info->vbe_bpp);
    serial_write_string_raw("\r\n");

    serial_write_string_raw("  framebuffer_phys="); serial_write_hex(info->framebuffer_phys, 8);
    serial_write_string_raw("\r\n");
}

void debug_serial_plugin_init(const boot_info_t* info) {
    g_serial_port = (uint16_t)CONFIG_DEBUG_SERIAL_PORT;
    serial_hw_init();
    g_serial_ready = 1;
    g_heartbeat_divider = 0;
    g_heartbeat_sequence = 0;

    serial_write_line_raw("=== Mezereon serial debug plugin ===");
    serial_write_string_raw("port="); serial_write_hex(g_serial_port, 4);
    serial_write_string_raw(" baud="); serial_write_dec(CONFIG_DEBUG_SERIAL_BAUD);
    serial_write_string_raw(" cfg=8N1\r\n");
    serial_write_string_raw("heartbeat interval="); serial_write_dec(CONFIG_DEBUG_SERIAL_HEARTBEAT_TICKS);
    serial_write_string_raw(" ticks\r\n");

    debug_serial_log_bootinfo(info);
}

void debug_serial_plugin_putc(char c) {
    if (!g_serial_ready) return;
    serial_write_char_raw(c);
}

void debug_serial_plugin_write(const char* s) {
    if (!g_serial_ready) return;
    serial_write_string_raw(s);
}

void debug_serial_plugin_writeln(const char* s) {
    if (!g_serial_ready) return;
    serial_write_line_raw(s);
}

void debug_serial_plugin_write_hex16(uint16_t v) {
    if (!g_serial_ready) return;
    serial_write_hex(v, 4);
}

void debug_serial_plugin_write_hex32(uint32_t v) {
    if (!g_serial_ready) return;
    serial_write_hex(v, 8);
}

void debug_serial_plugin_write_dec(uint32_t v) {
    if (!g_serial_ready) return;
    serial_write_dec(v);
}

void debug_serial_plugin_timer_tick(void) {
    if (!g_serial_ready) return;
    g_heartbeat_divider++;
    if (g_heartbeat_divider >= (uint32_t)CONFIG_DEBUG_SERIAL_HEARTBEAT_TICKS) {
        g_heartbeat_divider = 0;
        g_heartbeat_sequence++;
        serial_write_string_raw("[heartbeat ");
        serial_write_dec(g_heartbeat_sequence);
        serial_write_string_raw("]\r\n");
    }
}

#endif
