#include "netface.h"
#include "drivers/ne2000.h"
#include "console.h"
#include <stdint.h>

// Minimal top-level NIC selector: NE2000 only for now
typedef enum { NETDRV_NONE = 0, NETDRV_NE2000 } netdrv_t;
static netdrv_t s_active = NETDRV_NONE;
static bool s_ne2k_present = false;
static bool s_ne2k_init_ok = false;

/* console used for diagnostics */

bool netface_init(void) {
    // Probe NE2000 presence for diagnostics
    s_ne2k_present = ne2000_present();

    // Try NE2000 first (ISA)
    if (s_ne2k_present) {
        if (ne2000_init()) {
            s_active = NETDRV_NE2000;
            s_ne2k_init_ok = true;
            return true;
        }
        s_ne2k_init_ok = false;
    }
    s_active = NETDRV_NONE;
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

static const char* drv_name(netdrv_t d) {
    switch (d) {
        case NETDRV_NE2000: return "NE2000";
        default: return "none";
    }
}

const char* netface_active_name(void) { return drv_name(s_active); }

static void phex8(unsigned char v) {
    const char* hexd = "0123456789ABCDEF";
    char s[3]; s[0]=hexd[(v>>4)&0xF]; s[1]=hexd[v&0xF]; s[2]=0; console_write(s);
}

void netface_diag_print(void) {
    // Active driver summary
    console_write("netface: active=");
    console_write(drv_name(s_active));
    console_write("\n");

    // NE2000 diagnostic line
    console_write("ne2000: present=");
    console_write(s_ne2k_present ? "yes" : "no");
    console_write(" init=");
    console_write(s_ne2k_init_ok ? "ok" : "no");
    console_write(" io=");
    if (s_ne2k_present) {
        unsigned short io = ne2000_io_base();
        console_write_hex16(io);
    } else {
        console_write("----");
    }
    console_write("\n");

    if (s_active == NETDRV_NE2000) {
        // MAC
        unsigned char mac[6];
        if (ne2000_get_mac(mac)) {
            console_write("mac=");
            for (int i=0;i<6;i++){ if(i) console_write(":"); phex8(mac[i]); }
            console_write("\n");
        }
        // Promisc
        console_write("promisc=");
        console_write(ne2000_is_promisc()?"on":"off");
        console_write("\n");
        // Link/Speed (NE2000 class)
        console_write("link=");
        console_write("unknown");
        console_write(" speed=");
        console_write("10Mbps");
        console_write(" (NE2000-class)\n");
    }
}

void netface_bootinfo_print(void) {
    // One line: driver, mac, promisc, io
    console_write("net: drv=");
    console_write(drv_name(s_active));

    if (s_active == NETDRV_NE2000) {
        unsigned char mac[6];
        if (ne2000_get_mac(mac)) {
            console_write(" mac=");
            for (int i=0;i<6;i++){ if(i) console_write(":"); phex8(mac[i]); }
        }
        console_write(" promisc=");
        console_write(ne2000_is_promisc()?"on":"off");
        console_write(" io=");
        console_write_hex16(ne2000_io_base());
    }
    console_write("\n");
}

bool netface_get_mac(unsigned char mac[6]) {
    if (s_active == NETDRV_NE2000) return ne2000_get_mac(mac);
    return false;
}

bool netface_send(const unsigned char* frame, unsigned short len) {
    if (s_active == NETDRV_NE2000) return ne2000_send(frame, len);
    return false;
}

// Forward incoming frames to the IPv4/ARP stack (implemented in net_ipv4.c)
void netface_on_rx(const unsigned char* frame, unsigned short len) {
    extern void net_ipv4_on_frame(const uint8_t* frame, uint16_t len);
    net_ipv4_on_frame((const uint8_t*)frame, (uint16_t)len);
}
