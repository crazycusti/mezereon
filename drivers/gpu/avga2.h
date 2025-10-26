#ifndef DRIVERS_GPU_AVGA2_H
#define DRIVERS_GPU_AVGA2_H

#include "gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void avga2_classify_info(gpu_info_t* info);
int  avga2_signature_present(void);
void avga2_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif // DRIVERS_GPU_AVGA2_H
