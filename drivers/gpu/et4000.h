#ifndef DRIVERS_GPU_ET4000_H
#define DRIVERS_GPU_ET4000_H

#include "gpu.h"
#include "../../display.h"

typedef enum {
    ET4000_MODE_640x480x8 = 0,
    ET4000_MODE_640x400x8 = 1
} et4000_mode_t;

int et4000_detect(gpu_info_t* out_info);
int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode);
void et4000_restore_text_mode(void);
void et4000_debug_dump(void);
void et4000_set_debug_trace(int enabled);

#endif // DRIVERS_GPU_ET4000_H
