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

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_delay(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

extern void video_print(const char* str);
extern void video_print_hex16(unsigned short v);
extern void video_print_dec(unsigned int v);

static uint16_t ne2k_base_io = (uint16_t)CONFIG_NE2000_IO;
static bool ne2k_use_16bit = (CONFIG_NE2000_PIO_16BIT != 0);

uint16_t ne2000_io_base(void) {
    return ne2k_base_io;
}

static bool ne2000_detect_at(uint16_t base) {
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
            // Ack reset
            outb(base + NE2K_REG_ISR, 0x80);
            ne2k_base_io = base;
            return true;
        }
    }
    return false;
}

bool ne2000_present(void) {
    if (ne2000_detect_at((uint16_t)CONFIG_NE2000_IO)) {
        return true;
    }

#if CONFIG_NE2000_SCAN
    // Scan a conservative list of common ISA bases
    const uint16_t scan_bases[] = { 0x300, 0x320, 0x340, 0x280 };
    for (unsigned i = 0; i < sizeof(scan_bases)/sizeof(scan_bases[0]); i++) {
        if (scan_bases[i] == (uint16_t)CONFIG_NE2000_IO) continue;
        if (ne2000_detect_at(scan_bases[i])) {
            return true;
        }
    }
#endif
    return false;
}

bool ne2000_init(void) {
    if (!ne2000_present()) {
        video_print("NE2000 not detected.\n");
        return false;
    }

    video_print("NE2000 detected at ");
    video_print_hex16(ne2k_base_io);
    video_print(".\n");

    // Stop the NIC (CR.STOP = 0x01) before further configuration
    outb(ne2k_base_io + NE2K_REG_CMD, 0x01);
    io_delay();

    // Minimal DCR setup
    // 16-bit: 0x49 (WTS=1, LS=1 little-endian, FT=01 => 8B FIFO)
    //  8-bit: 0x09 (WTS=0, LS=1, FT=01)
    outb(ne2k_base_io + NE2K_REG_DCR, ne2k_use_16bit ? 0x49 : 0x09);
    io_delay();

    // Basic transmit config: normal operation, mask all IRQs (polling)
    outb(ne2k_base_io + NE2K_REG_TCR, 0x00);
    outb(ne2k_base_io + NE2K_REG_IMR, 0x00);
    // Clear pending interrupts
    outb(ne2k_base_io + NE2K_REG_ISR, 0xFF);

    // Self-test: probe PIO width using station PROM read
    {
        video_print("NE2000 PIO self-test: try ");
        video_print(ne2k_use_16bit ? "16-bit\n" : "8-bit\n");

        uint8_t mac[6];
        bool ok = ne2000_read_mac(mac);
        if (!ok) {
            // toggle mode and retry
            ne2k_use_16bit = !ne2k_use_16bit;
            outb(ne2k_base_io + NE2K_REG_DCR, ne2k_use_16bit ? 0x49 : 0x09);
            io_delay();
            video_print("Fallback to ");
            video_print(ne2k_use_16bit ? "16-bit" : "8-bit");
            video_print("...\n");
            ok = ne2000_read_mac(mac);
        }

        if (ok) {
            video_print("PIO width OK: ");
            video_print(ne2k_use_16bit ? "16-bit\n" : "8-bit\n");
        } else {
            video_print("PIO probe failed, continuing with ");
            video_print(ne2k_use_16bit ? "16-bit\n" : "8-bit\n");
        }
    }

    // For now we stop here. Full ring buffer init will follow.
    return true;
}

static bool ne2000_wait_rdc(void) {
    // Wait for remote DMA complete with a bounded spin
    for (int i = 0; i < 65535; i++) {
        uint8_t isr = inb(ne2k_base_io + NE2K_REG_ISR);
        if (isr & NE2K_ISR_RDC) {
            outb(ne2k_base_io + NE2K_REG_ISR, NE2K_ISR_RDC); // ack
            return true;
        }
    }
    return false;
}

static void ne2000_remote_write(uint16_t dst, const uint8_t* buf, uint16_t len) {
    // Program remote DMA registers
    outb(ne2k_base_io + NE2K_REG_RSAR0, (uint8_t)(dst & 0xFF));
    outb(ne2k_base_io + NE2K_REG_RSAR1, (uint8_t)((dst >> 8) & 0xFF));

    uint16_t count = len;
    if (ne2k_use_16bit && (count & 1)) count++; // pad to even bytes for 16-bit

    outb(ne2k_base_io + NE2K_REG_RBCR0, (uint8_t)(count & 0xFF));
    outb(ne2k_base_io + NE2K_REG_RBCR1, (uint8_t)((count >> 8) & 0xFF));

    // Clear RDC
    outb(ne2k_base_io + NE2K_REG_ISR, NE2K_ISR_RDC);

    // Start remote DMA write (CR: STA=1, RD=010)
    outb(ne2k_base_io + NE2K_REG_CMD, 0x12);

    if (ne2k_use_16bit) {
        const uint16_t* p = (const uint16_t*)buf;
        uint16_t words = len >> 1;
        for (uint16_t i = 0; i < words; i++) {
            outw(ne2k_base_io + NE2K_REG_DATA, p[i]);
        }
        if (len & 1) {
            outb(ne2k_base_io + NE2K_REG_DATA, buf[len - 1]);
        }
    } else {
        for (uint16_t i = 0; i < len; i++) {
            outb(ne2k_base_io + NE2K_REG_DATA, buf[i]);
        }
    }

    (void)ne2000_wait_rdc();
}

static void ne2000_remote_read(uint16_t src, uint8_t* buf, uint16_t len) {
    outb(ne2k_base_io + NE2K_REG_RSAR0, (uint8_t)(src & 0xFF));
    outb(ne2k_base_io + NE2K_REG_RSAR1, (uint8_t)((src >> 8) & 0xFF));

    uint16_t count = len;
    if (ne2k_use_16bit && (count & 1)) count++;
    outb(ne2k_base_io + NE2K_REG_RBCR0, (uint8_t)(count & 0xFF));
    outb(ne2k_base_io + NE2K_REG_RBCR1, (uint8_t)((count >> 8) & 0xFF));

    // Clear RDC
    outb(ne2k_base_io + NE2K_REG_ISR, NE2K_ISR_RDC);

    // Start remote DMA read (CR: STA=1, RD=001)
    outb(ne2k_base_io + NE2K_REG_CMD, 0x0A);

    if (ne2k_use_16bit) {
        uint16_t* p = (uint16_t*)buf;
        uint16_t words = len >> 1;
        for (uint16_t i = 0; i < words; i++) {
            p[i] = inw(ne2k_base_io + NE2K_REG_DATA);
        }
        if (len & 1) {
            buf[len - 1] = inb(ne2k_base_io + NE2K_REG_DATA);
        }
    } else {
        for (uint16_t i = 0; i < len; i++) {
            buf[i] = inb(ne2k_base_io + NE2K_REG_DATA);
        }
    }

    (void)ne2000_wait_rdc();
}

static bool ne2000_read_mac(uint8_t mac[6]) {
    // Many NE2000 clones expose station PROM at 0x0000
    uint8_t tmp[12];
    ne2000_remote_read(0x0000, tmp, sizeof(tmp));

    // Some cards repeat each byte; pick even indices
    for (int i = 0; i < 6; i++) mac[i] = tmp[i * 2];

    // Basic sanity: not all 0x00 or 0xFF
    int sum = 0, ff = 0;
    for (int i = 0; i < 6; i++) { sum |= mac[i]; ff += (mac[i] == 0xFF); }
    if (sum == 0x00 || ff == 6) return false;
    return true;
}

static bool ne2000_tx_packet(const uint8_t* frame, uint16_t len) {
    if (len < 60) len = 60; // Minimum Ethernet frame length (without FCS)

    const uint8_t tpsr = 0x40;             // Transmit page start
    const uint16_t dst = ((uint16_t)tpsr) << 8; // Memory address in NIC RAM

    // Copy frame into NIC RAM via remote DMA write (waits for RDC)
    ne2000_remote_write(dst, frame, len);

    // Program transmitter
    outb(ne2k_base_io + NE2K_REG_TPSR, tpsr);
    outb(ne2k_base_io + NE2K_REG_TBCR0, (uint8_t)(len & 0xFF));
    outb(ne2k_base_io + NE2K_REG_TBCR1, (uint8_t)((len >> 8) & 0xFF));

    // Clear pending TX bits
    outb(ne2k_base_io + NE2K_REG_ISR, 0x0A); // PTX (0x02) | TXE (0x08)

    // Start transmitter (CR: STA=1, TXP=1)
    outb(ne2k_base_io + NE2K_REG_CMD, 0x06);

    // Poll for completion
    for (int i = 0; i < 65535; i++) {
        uint8_t isr = inb(ne2k_base_io + NE2K_REG_ISR);
        if (isr & 0x0A) { // PTX or TXE
            outb(ne2k_base_io + NE2K_REG_ISR, isr & 0x0A); // Ack
            return (isr & 0x02) != 0; // true if PTX
        }
    }
    return false;
}

bool ne2000_send_test(void) {
    uint8_t mac[6];
    if (!ne2000_read_mac(mac)) {
        // Fallback LAA
        mac[0]=0x02; mac[1]=0x00; mac[2]=0x00; mac[3]=0x00; mac[4]=0x00; mac[5]=0x01;
    }

    // Build simple Ethernet broadcast with custom ethertype
    uint8_t frame[64];
    for (int i=0;i<6;i++) frame[i] = 0xFF;        // DST = broadcast
    for (int i=0;i<6;i++) frame[6+i] = mac[i];    // SRC
    frame[12] = 0x88; frame[13] = 0xB5;           // Ethertype: experimental

    const char msg[] = "MEZEREON TEST";
    int payload_len = (int)sizeof(msg)-1; // exclude NUL
    for (int i=0;i<payload_len;i++) frame[14+i] = (uint8_t)msg[i];
    uint16_t len = 14 + (uint16_t)payload_len;

    // Pad to 60 bytes min (NIC will add FCS)
    while (len < 60) frame[len++] = 0x00;

    bool ok = ne2000_tx_packet(frame, len);
    video_print(ok ? "Sent test frame.\n" : "Send failed.\n");
    return ok;
}
