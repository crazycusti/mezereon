#pragma once
#include <stdint.h>
#include <stdbool.h>

// Minimal PC speaker (PIT channel 2) driver

// Initialize driver; returns true if basic I/O port access appears to work.
bool pcspeaker_init(void);

// Quick capability check after init
bool pcspeaker_present(void);

// Configure tone frequency (Hz). Does not enable gate.
void pcspeaker_set_freq(uint32_t hz);

// Gate control
void pcspeaker_on(void);
void pcspeaker_off(void);

// Convenience: generate a blocking beep at frequency for duration (ms)
void pcspeaker_beep(uint32_t hz, uint32_t duration_ms);

