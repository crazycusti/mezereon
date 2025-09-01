#include "main.h"
#include "shell.h"
#include "keyboard.h"
#include "platform.h"

void kmain()
{
    console_init();
    console_writeln("Initializing Mezereon... Video initialized.");
    // Platform init: IDT/PIC remap, PIT 100Hz, unmask required IRQs and enable
    platform_interrupts_init();
    platform_timer_init(100);
    platform_irq_unmask(0); // timer
    platform_irq_unmask(1); // keyboard
    platform_irq_unmask(3); // NE2000 (driver masks internally if needed)
    platform_interrupts_enable();
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
