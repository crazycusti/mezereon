#pragma once
#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
// Returns ASCII char or -1 if no key available.
int keyboard_poll_char(void);
// Enable IRQ-driven mode (no HW polling when enabled)
void keyboard_set_irq_mode(int enabled);
// IRQ path: enqueue raw scancode from IRQ1 handler
void keyboard_isr_byte(uint8_t sc);
// Debug helper: dumps recent raw scancodes to the console
void keyboard_debug_dump(void);
int keyboard_wait_key(void);

// Special key codes (>255) returned by keyboard_poll_char when applicable
#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104
#define KEY_PGUP  0x105
#define KEY_PGDN  0x106
