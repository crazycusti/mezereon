#include "ne2000.h"

// Local I/O helpers (freestanding)
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_delay(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

extern void video_print(const char* str);

bool ne2000_present(void) {
    const uint16_t base = (uint16_t)CONFIG_NE2000_IO;

    // Trigger reset: many NE2000 clones reset on read of RESET reg; some on write.
    (void)inb(base + NE2K_REG_RESET);
    io_delay();

    // Poll ISR for RST bit (bit 7)
    for (int i = 0; i < 65535; i++) {
        uint8_t isr = inb(base + NE2K_REG_ISR);
        if (isr & 0x80) {
            // Ack reset
            outb(base + NE2K_REG_ISR, 0x80);
            return true;
        }
    }
    return false;
}

void ne2000_init(void) {
    if (ne2000_present()) {
        video_print("NE2000 detected. ");
    } else {
        video_print("NE2000 not detected. ");
        return;
    }

    // Stop the NIC (CR.STOP = 0x01) before further configuration
    outb((uint16_t)CONFIG_NE2000_IO + NE2K_REG_CMD, 0x01);
    io_delay();

    // Minimal DCR setup for 16-bit transfers, little-endian, FIFO 8 bytes
    // 0x49: WTS=1 (16-bit), LS=1 (little endian), FT1:0=01 (8 byte)
    outb((uint16_t)CONFIG_NE2000_IO + NE2K_REG_DCR, 0x49);
    io_delay();

    // For now we stop here. Full ring buffer init will follow.
}
