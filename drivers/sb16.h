#pragma once
#ifndef DRIVERS_SB16_H
#define DRIVERS_SB16_H

#include <stdint.h>
#include <stdbool.h>

// Minimal Sound Blaster 16 legacy ISA helper (detection + metadata)
// Future work: DMA playback hooks.

typedef struct {
    uint16_t base_port;
    uint8_t irq;
    uint8_t dma8;
    uint8_t dma16;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t present;
    uint8_t reserved;
} sb16_info_t;

// Probe for SB16 hardware on legacy ISA ports (uses reset handshake).
// Safe to call multiple times; later calls reuse cached result.
void sb16_init(void);

bool sb16_present(void);

const sb16_info_t* sb16_get_info(void);

// Placeholder for future PCM submission path; currently returns -1.
int sb16_pcm_submit(const void* data, uint32_t length);

#endif // DRIVERS_SB16_H
