#include "netface.h"
#include "drivers/ne2000.h"

// Minimal top-level NIC selector: NE2000 only for now
typedef enum { NETDRV_NONE = 0, NETDRV_NE2000 } netdrv_t;
static netdrv_t s_active = NETDRV_NONE;
static bool s_ne2k_present = false;
static bool s_ne2k_init_ok = false;

extern void video_print(const char* str);

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
    char s[3]; s[0]=hexd[(v>>4)&0xF]; s[1]=hexd[v&0xF]; s[2]=0; video_print(s);
}

void netface_diag_print(void) {
    extern void video_print_hex16(unsigned short);
    extern void video_println(const char*);

    // Active driver summary
    video_print("netface: active=");
    video_print(drv_name(s_active));
    video_print("\n");

    // NE2000 diagnostic line
    video_print("ne2000: present=");
    video_print(s_ne2k_present ? "yes" : "no");
    video_print(" init=");
    video_print(s_ne2k_init_ok ? "ok" : "no");
    video_print(" io=");
    if (s_ne2k_present) {
        unsigned short io = ne2000_io_base();
        video_print_hex16(io);
    } else {
        video_print("----");
    }
    video_print("\n");

    if (s_active == NETDRV_NE2000) {
        // MAC
        unsigned char mac[6];
        if (ne2000_get_mac(mac)) {
            video_print("mac=");
            for (int i=0;i<6;i++){ if(i) video_print(":"); phex8(mac[i]); }
            video_print("\n");
        }
        // Promisc
        video_print("promisc=");
        video_print(ne2000_is_promisc()?"on":"off");
        video_print("\n");
        // Link/Speed (NE2000 class)
        video_print("link=");
        video_print("unknown");
        video_print(" speed=");
        video_print("10Mbps");
        video_print(" (NE2000-class)\n");
    }

    (void)video_println; // silence unused if macros change
}

void netface_bootinfo_print(void) {
    extern void video_print_hex16(unsigned short);
    // One line: driver, mac, promisc, io
    video_print("net: drv=");
    video_print(drv_name(s_active));

    if (s_active == NETDRV_NE2000) {
        unsigned char mac[6];
        if (ne2000_get_mac(mac)) {
            video_print(" mac=");
            for (int i=0;i<6;i++){ if(i) video_print(":"); phex8(mac[i]); }
        }
        video_print(" promisc=");
        video_print(ne2000_is_promisc()?"on":"off");
        video_print(" io=");
        video_print_hex16(ne2000_io_base());
    }

    video_print("\n");
}
