#include "config.h"
#ifndef NETFACE_H
#define NETFACE_H

#include <stdbool.h>

// Top-level network interface abstraction.

// Initialize and select an active NIC driver (currently NE2000 only).
// Returns true if a NIC was initialized and is usable.
bool netface_init(void);

// Background progress/service function (drains RX rings, etc.).
void netface_poll(void);

// Verbose RX dump (for shell command netrxdump).
void netface_poll_rx(void);

// Send a small test frame (driver-provided implementation).
bool netface_send_test(void);

// IRQ handler hook (e.g., called from IRQ3 in this build).
void netface_irq(void);

#endif // NETFACE_H

