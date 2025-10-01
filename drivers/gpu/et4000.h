#ifndef DRIVERS_GPU_ET4000_H
#define DRIVERS_GPU_ET4000_H

#include "gpu.h"
#include "../../display.h"

int et4000_detect(gpu_info_t* out_info);
int et4000_set_mode_640x480x8(gpu_info_t* gpu, display_mode_info_t* out_mode);
void et4000_restore_text_mode(void);

#endif // DRIVERS_GPU_ET4000_H
