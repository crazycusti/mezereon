#include "interrupts.h"
#include "config.h"

// I/O helpers
static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

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
extern void video_print(const char*);
extern void video_print_dec(unsigned int);
// For IRQ1 keyboard
#include "keyboard.h"
// For IRQ3 NE2000 ack (optional)
#include "drivers/ne2000.h"

void irq0_handler_c(void) {
    ticks++;
    // Update a small time indicator at top-right ~10 Hz
    if ((ticks % 10u) == 0u) {
        uint32_t t = ticks; // copy
        uint32_t sec = t / 100u;
        uint32_t tenths = (t % 100u) / 10u;

        // Render "T 12345.6s" right-aligned in first row
        char buf[12];
        int pos = 0;
        buf[pos++] = 'T'; buf[pos++] = ' ';
        // decimal seconds
        char tmp[10]; int ti=0;
        if (sec == 0) { tmp[ti++]='0'; }
        else {
            while (sec && ti < 10) { tmp[ti++] = (char)('0' + (sec % 10)); sec/=10; }
        }
        while (ti--) buf[pos++] = tmp[ti];
        buf[pos++] = '.'; buf[pos++] = (char)('0' + (int)tenths);
        buf[pos++] = 's';
        int len = pos;

        // write to VGA without touching cursor/state
        volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
        int start_col = CONFIG_VGA_WIDTH - len;
        if (start_col < 0) start_col = 0;
        for (int i = 0; i < len; i++) {
            vga[i + start_col] = (uint16_t)((0x1F << 8) | (uint8_t)buf[i]);
        }
    }
    // Acknowledge PIC
    outb(0x20, 0x20);
}

void irq1_handler_c(void) {
    // Read scancode to deassert IRQ and queue for polling path
    uint8_t sc = inb(0x60);
    // optional: read status (0x64) to clear
    (void)inb(0x64);
    keyboard_isr_byte(sc);
    outb(0x20, 0x20);
}

void irq3_handler_c(void) {
    // If NE2000 present and has pending bits, ack them; otherwise just EOI
    uint16_t base = ne2000_io_base();
    if (base) {
        uint8_t isr = inb((uint16_t)(base + NE2K_REG_ISR));
        if (isr && isr != 0xFF) {
            outb((uint16_t)(base + NE2K_REG_ISR), isr);
        }
    }
    outb(0x20, 0x20);
}
