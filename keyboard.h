#pragma once
#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
// Returns ASCII char or -1 if no key available.
int keyboard_poll_char(void);
