#include "main.h"
#include "shell.h"
#include "keyboard.h"

void kmain()
{
    video_init();
    video_println("Initializing Mezereon... Video initialized.");
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
