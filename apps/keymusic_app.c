#include "../mezapi.h"

// Simple keymusic demo using MezAPI only
// Controls: C D E F G A H(B); Ctrl+Q to quit

int keymusic_app_main(const mez_api32_t* api){
    if (!api || api->abi_version < MEZ_ABI32_V1) return -1;
    if (api->console_writeln) api->console_writeln("keymusic (API): C D E F G A H(B); Ctrl+Q quits");
    for(;;){
        int k = api->input_poll_key ? api->input_poll_key() : -1;
        if (k < 0) { continue; }
        if (k == 0x11) { // Ctrl+Q
            if (api->console_write) api->console_write("\n");
            break;
        }
        unsigned f = 0;
        switch (k) {
            case 'c': case 'C': f = 262; break;
            case 'd': case 'D': f = 294; break;
            case 'e': case 'E': f = 330; break;
            case 'f': case 'F': f = 349; break;
            case 'g': case 'G': f = 392; break;
            case 'a': case 'A': f = 440; break;
            case 'h': case 'H': f = 494; break;
            case 'b': case 'B': f = 494; break;
            default: break;
        }
        if (f) {
            if (api->sound_beep) api->sound_beep(f, 150);
        }
    }
    return 0;
}
