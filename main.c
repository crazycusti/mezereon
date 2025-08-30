#include "main.h"

void kmain()
{
    video_init();
    video_print("Initializing Mezereon... Video initialized.");
    network_init();
    video_print("Networkstack initialized.");
    if (ne2000_init()) {
        video_print("NE2000 network interface initialized.");
    } else {
        video_print("NE2000 network interface not present.");
    }
    video_print("Welcome to Mezereon.");
  
    return;
}
