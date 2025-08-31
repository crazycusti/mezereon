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
static const uint8_t NE2K_RX_PSTART = 0x46; // leave 0x40.. for TX
static const uint8_t NE2K_RX_PSTOP  = 0x80; // end of 32KiB window

// Forward declarations for statics used before definition
static bool ne2000_read_mac(uint8_t mac[6]);

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

    // Basic TX/RX config
    outb(ne2k_base_io + NE2K_REG_TCR, 0x00);            // normal TX
    outb(ne2k_base_io + NE2K_REG_IMR, 0x00);            // mask all IRQs for now
    // Clear pending interrupts
    outb(ne2k_base_io + NE2K_REG_ISR, 0xFF);

    // Set receive configuration: accept broadcast only (AB)
    outb(ne2k_base_io + NE2K_REG_RCR, 0x04);

    // Program ring buffer boundaries
    outb(ne2k_base_io + NE2K_REG_PSTART, NE2K_RX_PSTART);
    outb(ne2k_base_io + NE2K_REG_PSTOP,  NE2K_RX_PSTOP);
    outb(ne2k_base_io + NE2K_REG_BNRY,   (uint8_t)(NE2K_RX_PSTART));

    // Switch to Page 1 to set CURR
    uint8_t cr = inb(ne2k_base_io + NE2K_REG_CMD);
    outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)((cr & 0x3F) | (1u<<6))); // PS=1
    outb(ne2k_base_io + 0x07, (uint8_t)(NE2K_RX_PSTART + 1)); // CURR
    // Back to Page 0
    outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)(cr & 0x3F));

    // Start the NIC (CR.STA)
    outb(ne2k_base_io + NE2K_REG_CMD, 0x02);

    // Enable NE2000 IRQs: PRX|PTX|RXE|TXE (OVW/CNT optional later)
    outb(ne2k_base_io + NE2K_REG_IMR, (uint8_t)(NE2K_ISR_PRX | NE2K_ISR_PTX | NE2K_ISR_RXE | NE2K_ISR_TXE));

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

static inline void print_hex8(uint8_t v) {
    const char* hexd = "0123456789ABCDEF";
    char s[3]; s[0]=hexd[(v>>4)&0xF]; s[1]=hexd[v&0xF]; s[2]=0; video_print(s);
}

static void ne2000_dump_eth(const uint8_t* buf, uint16_t len) {
    if (len < 14) return;
    uint16_t eth = ((uint16_t)buf[12] << 8) | buf[13];
    video_print("RX eth=0x");
    video_print_hex16(eth);
    video_print(" len=");
    video_print_dec(len);
    video_print(" src=");
    for (int i=0;i<6;i++){ if(i) video_print(":"); print_hex8(buf[6+i]); }
    video_print(" dst=");
    for (int i=0;i<6;i++){ if(i) video_print(":"); print_hex8(buf[i]); }
    video_print("\n");
}

void ne2000_poll_rx(void) {
    // Read boundary and current pointers
    uint8_t bnry = inb(ne2k_base_io + NE2K_REG_BNRY);
    // Switch to Page 1 to read CURR
    uint8_t cr = inb(ne2k_base_io + NE2K_REG_CMD);
    outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)((cr & 0x3F) | (1u<<6)));
    uint8_t curr = inb(ne2k_base_io + 0x07);
    // Back to Page 0
    outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)(cr & 0x3F));

    while (bnry != (uint8_t)(curr - 1 == 0xFF ? (NE2K_RX_PSTOP - 1) : (curr - 1))) {
        uint8_t packet_page = (uint8_t)(bnry + 1);
        if (packet_page >= NE2K_RX_PSTOP) packet_page = NE2K_RX_PSTART;
        uint16_t hdr_addr = ((uint16_t)packet_page) << 8;

        uint8_t hdr[4];
        ne2000_remote_read(hdr_addr, hdr, 4);
        uint8_t next = hdr[1];
        uint16_t count = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);

        // Read first 64 bytes of payload for summary (avoid large copies)
        uint16_t payload_addr = hdr_addr + 4;
        uint8_t buf[64];
        uint16_t copy = (count > sizeof(buf)) ? (uint16_t)sizeof(buf) : count;
        if (copy >= 14) {
            ne2000_remote_read(payload_addr, buf, copy);
            ne2000_dump_eth(buf, copy);
        }

        // Advance BNRY to previous page of 'next'
        uint8_t new_bnry = (next == NE2K_RX_PSTART) ? (NE2K_RX_PSTOP - 1) : (uint8_t)(next - 1);
        outb(ne2k_base_io + NE2K_REG_BNRY, new_bnry);
        bnry = new_bnry;

        // Refresh CURR for looping
        cr = inb(ne2k_base_io + NE2K_REG_CMD);
        outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)((cr & 0x3F) | (1u<<6)));
        curr = inb(ne2k_base_io + 0x07);
        outb(ne2k_base_io + NE2K_REG_CMD, (uint8_t)(cr & 0x3F));
    }

    // Ack PRX if set
    uint8_t isr = inb(ne2k_base_io + NE2K_REG_ISR);
    if (isr & NE2K_ISR_PRX) outb(ne2k_base_io + NE2K_REG_ISR, NE2K_ISR_PRX);
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
