#include "mezapi.h"
#include "console.h"
#include "keyboard.h"
#include "platform.h"
#include "drivers/pcspeaker.h"
#include <stdint.h>

// Minimal backend wrappers (text-mode drawing via VGA text memory for now)

#define VGA_MEM ((volatile uint16_t*)0xB8000)
#ifndef CONFIG_VGA_WIDTH
#define CONFIG_VGA_WIDTH 80
#endif
#ifndef CONFIG_VGA_HEIGHT
#define CONFIG_VGA_HEIGHT 25
#endif

static void api_text_put(int x, int y, char ch, uint8_t attr){
    if (x<0||y<0||x>=CONFIG_VGA_WIDTH||y>=CONFIG_VGA_HEIGHT) return;
    VGA_MEM[y*CONFIG_VGA_WIDTH + x] = (uint16_t)(((uint16_t)attr<<8) | (uint8_t)ch);
}

static void api_text_fill_line(int y, char ch, uint8_t attr){
    if (y<0||y>=CONFIG_VGA_HEIGHT) return;
    for (int x=0;x<CONFIG_VGA_WIDTH;x++) api_text_put(x,y,ch,attr);
}

static void api_time_sleep_ms(uint32_t ms){
    uint32_t hz = platform_timer_get_hz(); if (!hz) hz=100;
    uint32_t start = platform_ticks_get();
    uint32_t ticks = (ms * hz + 999u) / 1000u;
    while ((platform_ticks_get() - start) < ticks) { /* spin */ }
}

static void api_sound_tone_on(uint32_t hz){ if (hz){ pcspeaker_set_freq(hz); pcspeaker_on(); } }
static void api_sound_tone_off(void){ pcspeaker_off(); }

static const mez_api32_t g_api = {
    .abi_version     = MEZ_ABI32_V1,
    .size            = sizeof(mez_api32_t),
    .arch            = MEZ_ARCH_X86_32,

    .console_write   = console_write,
    .console_writeln = console_writeln,
    .console_clear   = console_clear,

    .input_poll_key  = keyboard_poll_char,

    .time_ticks_get  = platform_ticks_get,
    .time_timer_hz   = platform_timer_get_hz,
    .time_sleep_ms   = api_time_sleep_ms,

    .sound_beep      = pcspeaker_beep,
    .sound_tone_on   = api_sound_tone_on,
    .sound_tone_off  = api_sound_tone_off,

    .text_put        = api_text_put,
    .text_fill_line  = api_text_fill_line,
    .status_left     = console_status_set_left,
    .status_right    = console_draw_status_right,
};

const mez_api32_t* mez_api_get(void){ return &g_api; }

