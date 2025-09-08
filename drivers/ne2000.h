#include "../config.h"
#ifndef NE2000_H
#define NE2000_H

#include <stdint.h>
#include <stdbool.h>

// NE2000 (DP8390) register offsets relative to I/O base
#define NE2K_REG_CMD    0x00
#define NE2K_REG_ISR    0x07
#define NE2K_REG_DCR    0x0E
#define NE2K_REG_IMR    0x0F
#define NE2K_REG_RESET  0x1F

// Additional DP8390 registers (Page 0)
#define NE2K_REG_PSTART 0x01
#define NE2K_REG_PSTOP  0x02
#define NE2K_REG_BNRY   0x03
#define NE2K_REG_TPSR   0x04
#define NE2K_REG_TBCR0  0x05
#define NE2K_REG_TBCR1  0x06
#define NE2K_REG_RSAR0  0x08
#define NE2K_REG_RSAR1  0x09
#define NE2K_REG_RBCR0  0x0A
#define NE2K_REG_RBCR1  0x0B
#define NE2K_REG_RCR    0x0C
#define NE2K_REG_TCR    0x0D
#define NE2K_REG_DATA   0x10

// Page 1 register offsets (selected via CR PS=1)
// Note: same base + offset as Page 0, but different mapping.
#define NE2K_P1_PAR0    0x01  // Physical Address Register 0..5
#define NE2K_P1_CURR    0x07  // Current Page Register
#define NE2K_P1_MAR0    0x08  // Multicast Address Register 0..7

// ISR bits
#define NE2K_ISR_PRX 0x01
#define NE2K_ISR_PTX 0x02
#define NE2K_ISR_RXE 0x04
#define NE2K_ISR_TXE 0x08
#define NE2K_ISR_OVW 0x10
#define NE2K_ISR_CNT 0x20
#define NE2K_ISR_RDC 0x40
#define NE2K_ISR_RST 0x80

bool ne2000_present(void);
bool ne2000_init(void);
uint16_t ne2000_io_base(void);
bool ne2000_send_test(void);
void ne2000_poll_rx(void);
// IRQ ack/latch (called from top-level IRQ handler via netface)
void ne2000_irq(void);

// Diagnostics
bool ne2000_get_mac(uint8_t mac[6]);
bool ne2000_is_promisc(void);
// Transmit a raw Ethernet frame (len >= 60, FCS appended by NIC). Returns true on success.
bool ne2000_send(const uint8_t* frame, uint16_t len);

// IRQ cooperation: latch NIC ISR bits seen in IRQ3
void ne2000_isr_latch_or(uint8_t bits);
uint8_t ne2000_isr_take(uint8_t mask);

// Background service: handle latched IRQ events, drain RX ring
void ne2000_service(void);

#endif // NE2000_H
