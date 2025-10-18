// Minimal IDT/PIC/PIT setup for i386
#pragma once
#include <stdint.h>

void idt_init(void);
void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_set_mask(uint8_t irq, int masked);
void pic_mask_all(void);
void pit_init(uint32_t hz);
void interrupts_enable(void);
void interrupts_disable(void);
uint32_t interrupts_save_disable(void);
void interrupts_restore(uint32_t flags);

// Tick counter from PIT IRQ0
uint32_t ticks_get(void);
