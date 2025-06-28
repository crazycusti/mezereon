#include "ne2000.h"
#include <stdint.h>

// ne2000 testfunctions
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void ne2000_init() {
    uint8_t val = inb(CONFIG_NE2000_IO);
    (void)val; 
}
