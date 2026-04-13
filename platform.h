#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// Initialize CPU interrupts/PIC mapping. Architecture-specific.
void platform_interrupts_init(void);

// Quiesce legacy hardware (e.g. stop floppy motor)
void platform_quiesce_floppy(void);

// System reset
void platform_reboot(void);

// Initialize periodic timer to desired Hz (best-effort).
void platform_timer_init(uint32_t hz);
// Change timer frequency without altering IRQ masks
void platform_timer_set_hz(uint32_t hz);
uint32_t platform_timer_get_hz(void);

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

/* Busy-wait for an approximate millisecond delay (uses PIT ticks if running) */
void platform_delay_ms(uint32_t ms);

#endif // PLATFORM_H
