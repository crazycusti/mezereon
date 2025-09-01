#include "netface.h"
#include "drivers/ne2000.h"

// Minimal top-level NIC selector: NE2000 only for now
typedef enum { NETDRV_NONE = 0, NETDRV_NE2000 } netdrv_t;
static netdrv_t s_active = NETDRV_NONE;

extern void video_print(const char* str);

bool netface_init(void) {
    // Try NE2000 first (ISA)
    if (ne2000_present()) {
        if (ne2000_init()) {
            s_active = NETDRV_NE2000;
            return true;
        }
    }
    s_active = NETDRV_NONE;
    video_print("No NIC initialized.\n");
    return false;
}

void netface_poll(void) {
    switch (s_active) {
        case NETDRV_NE2000: ne2000_service(); break;
        default: break;
    }
}

void netface_poll_rx(void) {
    switch (s_active) {
        case NETDRV_NE2000: ne2000_poll_rx(); break;
        default: break;
    }
}

bool netface_send_test(void) {
    switch (s_active) {
        case NETDRV_NE2000: return ne2000_send_test();
        default: return false;
    }
}

void netface_irq(void) {
    switch (s_active) {
        case NETDRV_NE2000:
            // Let the NE2000 driver acknowledge/latch its ISR bits
            ne2000_irq();
            break;
        default: break;
    }
}

