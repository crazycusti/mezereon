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

bool ne2000_present(void);
bool ne2000_init(void);
uint16_t ne2000_io_base(void);

#endif // NE2000_H
