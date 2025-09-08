#include "cpuidle.h"
#include "config.h"

static volatile uint32_t g_idle_wakeups = 0;

void cpuidle_init(void) {
    g_idle_wakeups = 0;
}

void cpuidle_idle(void) {
#if defined(CONFIG_ARCH_X86) && (CONFIG_ARCH_X86)
    __asm__ __volatile__("hlt");
#else
    // no-op on non-x86 for now
#endif
    g_idle_wakeups++;
}

uint32_t cpuidle_wakeups_get(void) {
    return g_idle_wakeups;
}

