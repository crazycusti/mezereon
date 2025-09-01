#include "../../platform.h"

// SPARC platform stubs (not built by default). Replace with real OBP/IRQ impl.

void platform_interrupts_init(void) {}
void platform_timer_init(unsigned int hz) { (void)hz; }
void platform_irq_set_mask(unsigned char irq, int masked) { (void)irq; (void)masked; }
void platform_irq_mask_all(void) {}
void platform_interrupts_enable(void) {}
void platform_interrupts_disable(void) {}
unsigned int platform_ticks_get(void) { return 0; }

