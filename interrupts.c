#include "interrupts.h"
#include "config.h"
#include "arch/x86/io.h"
#include "console.h"
#include "cpuidle.h"
#include "debug_serial.h"
#include "platform.h"

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
static volatile uint32_t g_statusbar_pending_ticks;
static volatile uint32_t g_statusbar_last_update_tick;

static inline uint32_t statusbar_update_interval_ticks(void) {
    uint32_t hz = platform_timer_get_hz();
    if (hz == 0u) {
        hz = 100u;
    }
    uint32_t interval = hz / 10u;
    if (interval == 0u) {
        interval = 1u;
    }
    return interval;
}
uint32_t ticks_get(void){ return ticks; }

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t div = 1193182u / hz;
    outb(0x43, 0x36); // ch0, lo/hi, mode 3 (square)
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
}

void interrupts_enable(void){ __asm__ volatile("sti" ::: "memory"); }
void interrupts_disable(void){ __asm__ volatile("cli" ::: "memory"); }

uint32_t interrupts_save_disable(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl\n\tcli\n\tpopl %0" : "=r"(flags) : : "memory");
    return flags;
}

void interrupts_restore(uint32_t flags) {
    if (flags & (1u << 9)) {
        interrupts_enable();
    }
}

// C handler for IRQ0 (timer)
// For IRQ1 keyboard
#include "keyboard.h"
// Top-level NIC IRQ handler
#include "netface.h"

void irq0_handler_c(void) {
    ticks++;
    debug_serial_plugin_timer_tick();
    uint32_t t = ticks;
    uint32_t interval = statusbar_update_interval_ticks();
    if ((uint32_t)(t - g_statusbar_last_update_tick) >= interval) {
        g_statusbar_last_update_tick = t;
        g_statusbar_pending_ticks = t;
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

void interrupts_statusbar_poll(void) {
    uint32_t pending = 0;
    uint32_t flags = interrupts_save_disable();
    if (g_statusbar_pending_ticks != 0u) {
        pending = g_statusbar_pending_ticks;
        g_statusbar_pending_ticks = 0;
    }
    interrupts_restore(flags);
    if (pending == 0u) {
        return;
    }

    uint32_t hz = platform_timer_get_hz();
    if (hz == 0u) {
        hz = 100u;
    }
    uint32_t sec = pending / hz;
    uint32_t tenths = (pending % hz) * 10u / hz;
    if (tenths > 9u) {
        tenths = 9u;
    }

    char buf[32];
    int pos = 0;
    buf[pos++] = 'T';
    buf[pos++] = ' ';

    char tmp[10];
    int ti = 0;
    if (sec == 0u) {
        tmp[ti++] = '0';
    } else {
        while (sec && ti < (int)sizeof(tmp)) {
            tmp[ti++] = (char)('0' + (sec % 10u));
            sec /= 10u;
        }
    }
    while (ti--) {
        buf[pos++] = tmp[ti];
    }
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (int)tenths);
    buf[pos++] = 's';
    buf[pos++] = ' ';
    buf[pos++] = 'I';
    buf[pos++] = ' ';

    uint32_t wakeups = cpuidle_wakeups_get();
    ti = 0;
    if (wakeups == 0u) {
        tmp[ti++] = '0';
    } else {
        while (wakeups && ti < (int)sizeof(tmp)) {
            tmp[ti++] = (char)('0' + (wakeups % 10u));
            wakeups /= 10u;
        }
    }
    while (ti--) {
        buf[pos++] = tmp[ti];
    }
    int len = pos;
    console_draw_status_right(buf, len);
}

static void halt_forever(void) {
    while (1) {
        __asm__ volatile ("cli");
        __asm__ volatile ("hlt");
    }
}

void nmi_handler_c(void) {
    interrupts_disable();
    uint8_t status = inb(0x61);
    uint8_t parity = (uint8_t)((status >> 7) & 0x01u);
    uint8_t io_check = (uint8_t)((status >> 6) & 0x01u);

    console_writeln("NMI: non-maskable interrupt detected");
    console_write("  port 0x61 status=0x");
    console_write_hex16(status);
    console_write(" (parity=");
    console_write(parity ? "1" : "0");
    console_write(", io-check=");
    console_write(io_check ? "1" : "0");
    console_write(")\n");

    // Acknowledge by toggling bit 7 high then restoring.
    outb(0x61, (uint8_t)(status | 0x80u));
    outb(0x61, (uint8_t)(status & (uint8_t)~0x80u));

    console_writeln("  System halted after NMI.");
    halt_forever();
}

void page_fault_handler_c(uint32_t error_code, uint32_t fault_eip) {
    interrupts_disable();
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    console_writeln("PAGE FAULT: fatal fault encountered");
    console_write("  CR2 (fault addr)=0x");
    console_write_hex32(cr2);
    console_writeln("");
    console_write("  EIP              =0x");
    console_write_hex32(fault_eip);
    console_writeln("");
    console_write("  error            =0x");
    console_write_hex32(error_code);
    console_write(" (P=");
    console_write((error_code & 0x01u) ? "1" : "0");
    console_write(", W/R=");
    console_write((error_code & 0x02u) ? "1" : "0");
    console_write(", U/S=");
    console_write((error_code & 0x04u) ? "1" : "0");
    console_write(", RSVD=");
    console_write((error_code & 0x08u) ? "1" : "0");
    console_write(", IF=");
    console_write((error_code & 0x10u) ? "1" : "0");
    console_write(")\n");

    console_writeln("  Halting CPU to avoid triple fault.");
    halt_forever();
}

// General Protection Fault handler
void gpf_handler_c(uint32_t error_code, uint32_t eip) {
    interrupts_disable();
    debug_serial_plugin_write("[FATAL] General Protection Fault!\n");
    debug_serial_plugin_write("  Error code: 0x");
    debug_serial_plugin_write_hex32(error_code);
    debug_serial_plugin_write("\n  EIP: 0x");
    debug_serial_plugin_write_hex32(eip);
    debug_serial_plugin_write("\n");
    
    if (error_code & 1) debug_serial_plugin_write("  External event\n");
    if (error_code & 2) debug_serial_plugin_write("  IDT referenced\n");
    if (error_code & 4) debug_serial_plugin_write("  LDT referenced\n");
    else                debug_serial_plugin_write("  GDT referenced\n");
    
    uint16_t selector = (error_code >> 3) & 0x1FFF;
    debug_serial_plugin_write("  Selector index: 0x");
    debug_serial_plugin_write_hex16(selector);
    debug_serial_plugin_write("\n");
    
    debug_serial_plugin_write("Halting CPU to prevent triple fault.\n");
    while(1) __asm__ volatile("cli; hlt");
}

// Double Fault handler
void double_fault_handler_c(uint32_t error_code, uint32_t eip) {
    interrupts_disable();
    debug_serial_plugin_write("[FATAL] Double Fault!\n");
    debug_serial_plugin_write("  Error code: 0x");
    debug_serial_plugin_write_hex32(error_code);
    debug_serial_plugin_write("\n  EIP: 0x");
    debug_serial_plugin_write_hex32(eip);
    debug_serial_plugin_write("\n");
    debug_serial_plugin_write("Halting CPU to prevent triple fault.\n");
    while(1) __asm__ volatile("cli; hlt");
}

// Generic exception handler
void generic_exception_handler_c(void) {
    interrupts_disable();
    debug_serial_plugin_write("[WARN] Unhandled CPU exception\n");
    debug_serial_plugin_write("Halting CPU.\n");
    while(1) __asm__ volatile("cli; hlt");
}
