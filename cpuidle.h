#pragma once
#include <stdint.h>

// Initialize CPU idle handling
void cpuidle_init(void);

// Enter low-power idle until the next interrupt (x86: HLT). Increments wakeup counter when resumed.
void cpuidle_idle(void);

// Get accumulated wakeup count since init
uint32_t cpuidle_wakeups_get(void);
