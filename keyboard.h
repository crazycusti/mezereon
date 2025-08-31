#pragma once
#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
// Returns ASCII char or -1 if no key available.
int keyboard_poll_char(void);

// Special key codes (>255) returned by keyboard_poll_char when applicable
#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104
#define KEY_PGUP  0x105
#define KEY_PGDN  0x106
