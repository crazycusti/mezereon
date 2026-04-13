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
extern void irq1_stub(void);
extern void irq3_stub(void);
extern void nmi_stub(void);
extern void page_fault_stub(void);
extern void gpf_stub(void);
extern void double_fault_stub(void);
extern void generic_exception_stub(void);

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

    // Set up generic handler for all CPU exceptions (0-31)
    for (int i=0; i<32; i++) {
        idt_set_gate(i, (uint32_t)generic_exception_stub, 0x08, 0x8E);
    }

    // CPU exceptions we care about (override generic)
    idt_set_gate(2, (uint32_t)nmi_stub, 0x08, 0x8E);              // NMI
    idt_set_gate(8, (uint32_t)double_fault_stub, 0x08, 0x8E);     // Double Fault
    idt_set_gate(13, (uint32_t)gpf_stub, 0x08, 0x8E);             // General Protection Fault
    idt_set_gate(14, (uint32_t)page_fault_stub, 0x08, 0x8E);      // Page Fault

    // Map IRQs at vectors 0x20.. (PIC remap)
    idt_set_gate(32, (uint32_t)irq0_stub, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_stub, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3_stub, 0x08, 0x8E);

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)&idt[0];
    lidt(&idt[0], sizeof(idt)-1);
}
