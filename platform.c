#include "platform.h"
#include "interrupts.h"

void platform_interrupts_init(void) {
    // x86: basic IDT + PIC remap
    idt_init();
    pic_remap(0x20, 0x28);
}

void platform_timer_init(uint32_t hz) {
    // x86: PIT 8253
    pic_mask_all();
    pit_init(hz ? hz : 100);
}

static uint32_t g_timer_hz = 100;

void platform_timer_set_hz(uint32_t hz) {
    if (hz == 0) hz = 1;
    g_timer_hz = hz;
    pit_init(hz);
}

uint32_t platform_timer_get_hz(void) { return g_timer_hz; }

void platform_irq_set_mask(uint8_t irq, int masked) { pic_set_mask(irq, masked); }
void platform_irq_mask_all(void) { pic_mask_all(); }

void platform_interrupts_enable(void) { interrupts_enable(); }
void platform_interrupts_disable(void) { interrupts_disable(); }

uint32_t platform_ticks_get(void) { return ticks_get(); }
