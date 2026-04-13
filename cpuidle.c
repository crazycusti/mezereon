#include "cpuidle.h"
#include "config.h"
#include "interrupts.h"

static volatile uint32_t g_idle_wakeups = 0;

void cpuidle_init(void) {
    g_idle_wakeups = 0;
}

void cpuidle_idle(void) {
#if defined(CONFIG_ARCH_X86) && (CONFIG_ARCH_X86)
    // HLT only makes sense when interrupts are enabled; otherwise we might never resume.
    if (interrupts_are_enabled()) {
        __asm__ __volatile__("hlt");
    } else {
        __asm__ __volatile__("pause");
    }
#else
    // no-op on non-x86 for now
#endif
    g_idle_wakeups++;
}

uint32_t cpuidle_wakeups_get(void) {
    return g_idle_wakeups;
}
