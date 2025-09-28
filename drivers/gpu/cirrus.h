#ifndef DRIVERS_GPU_CIRRUS_H
#define DRIVERS_GPU_CIRRUS_H

#include "../../display.h"
#include "gpu.h"

int cirrus_gpu_detect(const pci_device_t* dev, gpu_info_t* out);
void cirrus_dump_state(const pci_device_t* dev);
int cirrus_set_mode_640x480x8(const pci_device_t* dev, display_mode_info_t* out_mode, gpu_info_t* gpu);
int cirrus_restore_text_mode(const pci_device_t* dev);

#endif // DRIVERS_GPU_CIRRUS_H
