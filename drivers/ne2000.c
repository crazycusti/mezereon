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

    // Quick 0xFF probe on common regs (floating bus reads as 0xFF)
    uint8_t p_isr = inb(base + NE2K_REG_ISR);
    uint8_t p_rst = inb(base + NE2K_REG_RESET);
    if (p_isr == 0xFF && p_rst == 0xFF) {
        return false;
    }

    // Trigger reset and look for ISR.RST
    (void)inb(base + NE2K_REG_RESET);
    io_delay();

    for (int i = 0; i < 65535; i++) {
        uint8_t isr = inb(base + NE2K_REG_ISR);
        if ((isr & 0x80) != 0 && isr != 0xFF) {
            // Ack reset; not strictly required to confirm presence
            outb(base + NE2K_REG_ISR, 0x80);
            return true;
        }
    }
    return false;
}

bool ne2000_init(void) {
    if (!ne2000_present()) {
        video_print("NE2000 not detected. ");
        return false;
    }

    video_print("NE2000 detected. ");

    // Stop the NIC (CR.STOP = 0x01) before further configuration
    outb((uint16_t)CONFIG_NE2000_IO + NE2K_REG_CMD, 0x01);
    io_delay();

    // Minimal DCR setup for 16-bit transfers, little-endian, FIFO 8 bytes
    // 0x49: WTS=1 (16-bit), LS=1 (little endian), FT1:0=01 (8 byte)
    outb((uint16_t)CONFIG_NE2000_IO + NE2K_REG_DCR, 0x49);
    io_delay();

    // For now we stop here. Full ring buffer init will follow.
    return true;
}
