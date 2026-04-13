#include "platform.h"
#include "interrupts.h"
#include "console.h"
#include "config.h"
#include "arch/x86/io.h"

static uint32_t g_timer_hz = 100;

void platform_quiesce_floppy(void) {
    // Write to the Digital Output Register (DOR) for Floppy Controller 1 (0x3F2).
    // Bits 4-7: Motor enables (0 = off)
    // Bit 3: DMA enable (1 = enable)
    // Bit 2: Reset (1 = normal operation, 0 = reset)
    // Bits 0-1: Drive select (0 = drive A)
    // A value of 0x0C (0000 1100) stops all motors, keeps FDC out of reset and enables DMA.
    outb(0x3F2, 0x0C);
    console_writeln("HW: floppy motor stop (DOR=0x0C)");
}

void platform_reboot(void) {
    interrupts_disable();
    // 1. Try 8042 keyboard controller pulse
    for (int guard = 0; guard < 100; guard++) {
        if ((inb(0x64) & 0x02u) == 0) break;
        io_delay();
    }
    outb(0x64, 0xFE); // Pulse reset line
    platform_delay_ms(50);
    
    // 2. Fallback: Triple fault by loading empty IDT and triggering INT
    struct { uint16_t len; uint32_t ptr; } idtr = { 0, 0 };
    __asm__ volatile ("lidt %0; int $3" : : "m"(idtr));
    
    // 3. Last resort: HLT loop
    while(1) __asm__ volatile("cli; hlt");
}

void platform_interrupts_init(void) {
    // x86: establish a valid IDT and remap the PIC even if we keep IRQs masked.
    // This prevents exceptions/IRQs from vectoring into the real-mode IVT layout.
    interrupts_disable();
    pic_mask_all();
    idt_init();
    pic_remap(0x20, 0x28);
    pic_mask_all();
    console_writeln("INT: IDT loaded, PIC remapped (IRQs masked)");
}

void platform_timer_init(uint32_t hz) {
#if CONFIG_BOOT_ENABLE_INTERRUPTS
    uint32_t effective = hz ? hz : 100u;
    g_timer_hz = effective;
    pit_init(effective);
#else
    (void)hz;
    // Leave timer "off" during bring-up so tick-based waits don't hang.
    g_timer_hz = 0;
#endif
}

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

void platform_delay_ms(uint32_t ms) {
    if (ms == 0) return;
    uint32_t start = ticks_get();
    uint32_t hz = platform_timer_get_hz();
    if (hz == 0) {
        /* Fallback: rough loop of I/O delays when timer is off */
        for (uint32_t i = 0; i < ms; i++) {
            for (uint32_t j = 0; j < 1000; j++) {
                __asm__ __volatile__("outb %%al, $0x80" :: "a"(0));
            }
        }
        return;
    }
    uint32_t target = start + ((ms * hz + 999u) / 1000u);
    while ((uint32_t)(ticks_get() - target) & 0x80000000u) { /* spin */ }
}
