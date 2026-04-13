#pragma once
#include <stdint.h>

// Initialize CPU idle handling
void cpuidle_init(void);

// Enter CPU idle (x86: HLT when interrupts are enabled; otherwise a non-blocking PAUSE).
// Increments wakeup counter on each call.
void cpuidle_idle(void);

// Get accumulated wakeup count since init
uint32_t cpuidle_wakeups_get(void);
