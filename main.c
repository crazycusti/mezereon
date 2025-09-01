#include "main.h"
#include "shell.h"
#include "keyboard.h"
#include "interrupts.h"

void kmain()
{
    console_init();
    console_writeln("Initializing Mezereon... Video initialized.");
    // Minimal IDT + PIC + PIT setup
    idt_init();
    pic_remap(0x20, 0x28);
    pic_mask_all();          // mask all IRQs first
    pit_init(100);          // 100 Hz
    pic_set_mask(0, 0);     // IRQ0 timer
    pic_set_mask(1, 0);     // IRQ1 keyboard
    pic_set_mask(3, 0);     // IRQ3 NE2000 (NIC still masked internally)
    interrupts_enable();
    keyboard_set_irq_mode(1);
    if (netface_init()) {
        console_write("Network interface initialized. Selected: ");
        console_write(netface_active_name());
        console_write("\n");
        netface_bootinfo_print();
        netface_send_test();
    } else {
        console_write("No network interface present. Selected: ");
        console_write(netface_active_name());
        console_write("\n");
        netface_bootinfo_print();
    }
    console_writeln("Welcome to Mezereon.");
    keyboard_init();
    shell_run();
}
