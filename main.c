#include "main.h"

void kmain()
{
    video_init();
    video_print("Initializing Mezereon... Video initialized.");
    network_init();
    video_print("Networkstack initialized.");
    ne2000_init();
    video_print("NE2000 network interface initialized.");
    video_print("Welcome to Mezereon.");
  
    return;
}
