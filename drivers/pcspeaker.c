#include "pcspeaker.h"
#include "../arch/x86/io.h"
#include "../platform.h"

// PIT (8253/8254) ports
#define PIT_CH2_DATA  0x42
#define PIT_CMD       0x43
// PC speaker control port
#define PCSPK_PORT    0x61

static bool s_present = false;

bool pcspeaker_present(void) { return s_present; }

static inline void pit_set_ch2_div(uint16_t div)
{
    // Mode 3 (square wave), binary, load lobyte/hibyte, select channel 2
    outb(PIT_CMD, 0xB6);
    outb(PIT_CH2_DATA, (uint8_t)(div & 0xFF));
    outb(PIT_CH2_DATA, (uint8_t)((div >> 8) & 0xFF));
}

void pcspeaker_set_freq(uint32_t hz)
{
    if (!hz) return;
    // PIT input clock is ~1_193_182 Hz
    uint32_t div = 1193182u / hz;
    if (div == 0) div = 1; // clamp
    if (div > 0xFFFF) div = 0xFFFF;
    pit_set_ch2_div((uint16_t)div);
}

void pcspeaker_on(void)
{
    // Set speaker enable (bit 0) and gate (bit 1)
    uint8_t v = inb(PCSPK_PORT);
    v |= 0x03;
    outb(PCSPK_PORT, v);
}

void pcspeaker_off(void)
{
    uint8_t v = inb(PCSPK_PORT);
    v &= (uint8_t)~0x03;
    outb(PCSPK_PORT, v);
}

bool pcspeaker_init(void)
{
    // Best-effort detection: toggle enable bit and read back
    uint8_t before = inb(PCSPK_PORT);
    outb(PCSPK_PORT, (uint8_t)(before ^ 0x02)); // flip gate bit as harmless probe
    uint8_t after  = inb(PCSPK_PORT);
    // Restore original state
    outb(PCSPK_PORT, before);
    s_present = (before != 0xFF) && (after != 0xFF);
    return s_present;
}

void pcspeaker_beep(uint32_t hz, uint32_t duration_ms)
{
    if (!s_present) return;
    if (hz == 0) hz = 880;
    if (duration_ms == 0) duration_ms = 100;
    pcspeaker_set_freq(hz);
    pcspeaker_on();
    // Busy-wait using platform ticks to keep it simple
    uint32_t start = platform_ticks_get();
    uint32_t t_hz = platform_timer_get_hz();
    if (!t_hz) t_hz = 100; // fallback assumption
    // Convert ms to ticks with rounding
    uint32_t wait_ticks = (duration_ms * t_hz + 999) / 1000u;
    while ((platform_ticks_get() - start) < wait_ticks) {
        // spin
    }
    pcspeaker_off();
}

