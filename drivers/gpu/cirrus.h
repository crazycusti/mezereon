#ifndef DRIVERS_GPU_CIRRUS_H
#define DRIVERS_GPU_CIRRUS_H

#include "../../display.h"
#include "gpu.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    const uint16_t* seq;
    const uint16_t* graph;
    const uint16_t* crtc;
} cirrus_mode_desc_t;

int cirrus_gpu_detect(const pci_device_t* dev, gpu_info_t* out);
void cirrus_dump_state(const pci_device_t* dev);
int cirrus_set_mode_640x480x8(const pci_device_t* dev, display_mode_info_t* out_mode, gpu_info_t* gpu);
int cirrus_restore_text_mode(const pci_device_t* dev);
const cirrus_mode_desc_t* cirrus_get_modes(size_t* count);
uint32_t cirrus_mode_vram_required(const cirrus_mode_desc_t* mode);
const cirrus_mode_desc_t* cirrus_find_mode(uint16_t width,
                                           uint16_t height,
                                           uint8_t bpp,
                                           uint32_t vram_bytes);
const cirrus_mode_desc_t* cirrus_default_mode(uint32_t vram_bytes);
int cirrus_set_mode_desc(const pci_device_t* dev,
                         const cirrus_mode_desc_t* mode,
                         display_mode_info_t* out_mode,
                         gpu_info_t* gpu);

#endif // DRIVERS_GPU_CIRRUS_H
