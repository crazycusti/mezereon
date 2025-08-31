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

// IRQ cooperation: latch NIC ISR bits seen in IRQ3
void ne2000_isr_latch_or(uint8_t bits);
uint8_t ne2000_isr_take(uint8_t mask);

// Background service: handle latched IRQ events, drain RX ring
void ne2000_service(void);

#endif // NE2000_H
