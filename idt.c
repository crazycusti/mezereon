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

// Double Fault TSS - required for handling double faults safely
struct __attribute__((packed)) tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
};

static struct tss_entry double_fault_tss;
static uint8_t double_fault_stack[4096];

extern void double_fault_tss_handler(void);

static void gdt_set_tss_gate(uint8_t num, uint32_t base, uint32_t limit);

void setup_double_fault_tss(void) {
    // Initialize Double Fault TSS
    __builtin_memset(&double_fault_tss, 0, sizeof(double_fault_tss));
    
    double_fault_tss.esp0 = (uint32_t)(double_fault_stack + sizeof(double_fault_stack));
    double_fault_tss.ss0 = 0x10;  // Data segment
    double_fault_tss.esp = (uint32_t)(double_fault_stack + sizeof(double_fault_stack));
    double_fault_tss.ss = 0x10;
    double_fault_tss.cs = 0x08;   // Code segment
    double_fault_tss.ds = 0x10;
    double_fault_tss.es = 0x10;
    double_fault_tss.fs = 0x10;
    double_fault_tss.gs = 0x10;
    double_fault_tss.eip = (uint32_t)double_fault_tss_handler;
    
    // Add TSS to GDT (assuming GDT has space at selector 0x28)
    gdt_set_tss_gate(5, (uint32_t)&double_fault_tss, sizeof(double_fault_tss) - 1);
    
    // Set IDT entry for double fault to use task gate
    idt[8].base_low = 0;
    idt[8].base_high = 0;
    idt[8].sel = 0x28;  // TSS selector
    idt[8].always0 = 0;
    idt[8].flags = 0x85;  // Task gate, present
}

// This will be called by the TSS
__attribute__((noreturn))
void double_fault_tss_handler(void) {
    // We're now in a clean task context
    __asm__ volatile("cli");
    
    // Simple infinite loop - we can't do much more
    while(1) {
        __asm__ volatile("hlt");
    }
}

// Stub - we need access to GDT which is typically in boot code
static void gdt_set_tss_gate(uint8_t num, uint32_t base, uint32_t limit) {
    // This is a placeholder - normally you'd modify the GDT here
    // For now, we'll skip this and use the interrupt gate approach
    (void)num; (void)base; (void)limit;
}
