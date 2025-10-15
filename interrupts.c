#include "interrupts.h"
#include "config.h"
#include "arch/x86/io.h"
#include "console.h"
#include "cpuidle.h"

// PIC remap as per OSDev
void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t a1 = inb(0x21);
    uint8_t a2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, offset1);
    outb(0xA1, offset2);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, a1);
    outb(0xA1, a2);
}

void pic_set_mask(uint8_t irq, int masked) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t  bit  = (uint8_t)(1u << (irq & 7));
    uint8_t  val  = inb(port);
    if (masked) val |= bit; else val &= (uint8_t)~bit;
    outb(port, val);
}

void pic_mask_all(void) {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

static volatile uint32_t ticks;
uint32_t ticks_get(void){ return ticks; }

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t div = 1193182u / hz;
    outb(0x43, 0x36); // ch0, lo/hi, mode 3 (square)
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
}

void interrupts_enable(void){ __asm__ volatile("sti"); }
void interrupts_disable(void){ __asm__ volatile("cli"); }

// C handler for IRQ0 (timer)
// For IRQ1 keyboard
#include "keyboard.h"
// Top-level NIC IRQ handler
#include "netface.h"

void irq0_handler_c(void) {
    ticks++;
    // Update a small time indicator at top-right ~10 Hz
    if ((ticks % 10u) == 0u) {
        uint32_t t = ticks; // copy
        uint32_t sec = t / 100u;
        uint32_t tenths = (t % 100u) / 10u;

        // Render "T 12345.6s I 12345" right-aligned in first row
        char buf[32];
        int pos = 0;
        buf[pos++] = 'T'; buf[pos++] = ' ';
        // decimal seconds
        char tmp[10]; int ti=0;
        if (sec == 0) { tmp[ti++]='0'; }
        else { while (sec && ti < 10) { tmp[ti++] = (char)('0' + (sec % 10)); sec/=10; } }
        while (ti--) buf[pos++] = tmp[ti];
        buf[pos++] = '.'; buf[pos++] = (char)('0' + (int)tenths);
        buf[pos++] = 's'; buf[pos++] = ' '; buf[pos++] = 'I'; buf[pos++] = ' ';
        // wakeups
        uint32_t w = cpuidle_wakeups_get(); ti=0;
        if (w == 0) { tmp[ti++]='0'; }
        else { while (w && ti < 10) { tmp[ti++] = (char)('0' + (w % 10)); w/=10; } }
        while (ti--) buf[pos++] = tmp[ti];
        int len = pos;

        // Write through the video module (no direct VGA access here)
        console_draw_status_right(buf, len);
    }
    // Acknowledge PIC
    outb(0x20, 0x20);
}

void irq1_handler_c(void) {
    uint8_t status = inb(0x64);
    if ((status & 0x01u) == 0) {
        outb(0x20, 0x20);
        return;
    }
    uint8_t sc = inb(0x60);
    if ((status & 0x20u) == 0) {
        keyboard_isr_byte(sc);
    }
    outb(0x20, 0x20);
}

void irq3_handler_c(void) {
    // Delegate to netface (driver-specific ack/latch), then EOI
    netface_irq();
    outb(0x20, 0x20);
}
