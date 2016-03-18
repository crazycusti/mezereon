#include "main.h"

int video()
{
    char video_adress = (char)0xb8000;  // farbdisplay ibm
    unsigned int displaysize = 80*25*2;
    video_adress = 0x07; // black/grey
