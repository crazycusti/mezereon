#ifndef DRIVERS_GPU_AVGA2_H
#define DRIVERS_GPU_AVGA2_H

#include "gpu.h"
#include "../../display.h"

#ifdef __cplusplus
extern "C" {
#endif

void avga2_classify_info(gpu_info_t* info);
int  avga2_signature_present(void);
void avga2_dump_state(void);
int  avga2_set_mode(gpu_info_t* gpu, display_mode_info_t* out_mode, uint16_t width, uint16_t height, uint8_t bpp);
int  avga2_restore_text_mode(void);

#ifdef __cplusplus
}
#endif

#endif // DRIVERS_GPU_AVGA2_H
