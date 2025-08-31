#include "main.h"
#include "shell.h"
#include "keyboard.h"
#include "interrupts.h"

void kmain()
{
    video_init();
    video_println("Initializing Mezereon... Video initialized.");
    // Minimal IDT + PIC + PIT setup
    idt_init();
    pic_remap(0x20, 0x28);
    pic_mask_all();          // mask all IRQs first
    pit_init(100);          // 100 Hz
    pic_set_mask(0, 0);     // unmask only IRQ0 (timer)
    interrupts_enable();
    network_init();
    video_println("Networkstack initialized.");
    if (ne2000_init()) {
        video_println("NE2000 network interface initialized.");
        ne2000_send_test();
    } else {
        video_println("NE2000 network interface not present.");
    }
    video_println("Welcome to Mezereon.");
    keyboard_init();
    shell_run();
}
