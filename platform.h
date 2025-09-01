#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// Initialize CPU interrupts/PIC mapping. Architecture-specific.
void platform_interrupts_init(void);

// Initialize periodic timer to desired Hz (best-effort).
void platform_timer_init(uint32_t hz);

// Mask/unmask specific IRQ line (PIC vector index on x86).
void platform_irq_set_mask(uint8_t irq, int masked);
static inline void platform_irq_unmask(uint8_t irq) { platform_irq_set_mask(irq, 0); }

// Mask all IRQ lines.
void platform_irq_mask_all(void);

// Global interrupt enable/disable
void platform_interrupts_enable(void);
void platform_interrupts_disable(void);

// Monotonic tick counter (if available)
uint32_t platform_ticks_get(void);

#endif // PLATFORM_H

