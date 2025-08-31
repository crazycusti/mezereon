#include "interrupts.h"
#include <stddef.h>

// IDT entry (32-bit interrupt gate)
struct __attribute__((packed)) idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags; // present, ring, gate type
    uint16_t base_high;
};

struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint32_t base;
};

extern void irq0_stub(void);

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static inline void lidt(void* base, uint16_t size) {
    struct __attribute__((packed)) { uint16_t len; uint32_t ptr; } idtr = { size, (uint32_t)base };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = (uint16_t)(base & 0xFFFF);
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags; // e.g., 0x8E
}

void idt_init(void) {
    // clear table: mark all entries not present by default
    for (int i=0;i<256;i++) {
        idt[i].base_low=0; idt[i].base_high=0; idt[i].sel=0x08; idt[i].always0=0; idt[i].flags=0x00;
    }

    // Map IRQ0 at vector 32 (0x20)
    idt_set_gate(32, (uint32_t)irq0_stub, 0x08, 0x8E);

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)&idt[0];
    lidt(&idt[0], sizeof(idt)-1);
}
