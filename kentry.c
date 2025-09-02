#include "bootinfo.h"
#include "config.h"

#if CONFIG_ARCH_X86
// Legacy kernel main (no bootinfo)
extern void kmain(void);
#endif

// New arch-neutral entry point.
void kentry(void* bi) {
    (void)bi;
#if CONFIG_ARCH_X86
    kmain();
#else
    // On non-x86 (e.g., SPARC boot stub), nothing to do yet.
    return;
#endif
}
