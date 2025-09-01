#include "bootinfo.h"

// Legacy kernel main (no bootinfo)
extern void kmain(void);

// New arch-neutral entry point. For now we ignore bootinfo and
// delegate to existing kmain() to preserve behavior.
void kentry(void* bi) {
    (void)bi;
    kmain();
}

