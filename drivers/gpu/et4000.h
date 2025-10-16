#ifndef DRIVERS_GPU_ET4000_H
#define DRIVERS_GPU_ET4000_H

#include "gpu.h"
#include "../../display.h"

typedef enum {
    ET4000_MODE_640x480x8 = 0,
    ET4000_MODE_640x400x8 = 1,
    ET4000_MODE_640x480x4 = 2,
    ET4000_MODE_MAX
} et4000_mode_t;

int et4000_detect(gpu_info_t* out_info);
int et4000_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, et4000_mode_t mode);
int et4k_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp);
et4000_mode_t et4k_choose_default_mode(int is_ax_variant, uint32_t vram_bytes);
void et4000_restore_text_mode(void);
void et4000_dump_bank(uint8_t bank, uint32_t offset, uint32_t length);
void et4000_debug_dump(void);
void et4000_set_debug_trace(int enabled);
int  et4k_debug_trace_enabled(void);
void et4k_disable_ax_engine(const char* reason);

#endif // DRIVERS_GPU_ET4000_H
