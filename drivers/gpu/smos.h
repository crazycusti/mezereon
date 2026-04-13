#ifndef DRIVERS_GPU_SMOS_H
#define DRIVERS_GPU_SMOS_H

#include "gpu.h"
#include "../../display.h"

int smos_detect(gpu_info_t* out_info);
int smos_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp);
void smos_restore_text_mode(void);

#endif // DRIVERS_GPU_SMOS_H
