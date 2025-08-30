#include "../config.h"
#ifndef NE2000_H
#define NE2000_H

#include <stdint.h>
#include <stdbool.h>

// NE2000 (DP8390) register offsets relative to I/O base
#define NE2K_REG_CMD    0x00
#define NE2K_REG_ISR    0x07
#define NE2K_REG_DCR    0x0E
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

bool ne2000_present(void);
bool ne2000_init(void);
uint16_t ne2000_io_base(void);
bool ne2000_send_test(void);

#endif // NE2000_H
