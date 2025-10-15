#ifndef DRIVERS_GPU_GPU_H
#define DRIVERS_GPU_GPU_H

#include <stdint.h>
#include <stddef.h>
#include "../pci.h"

#define GPU_MAX_DEVICES 4

typedef enum {
    GPU_TYPE_UNKNOWN = 0,
    GPU_TYPE_CIRRUS,
    GPU_TYPE_ET4000,
    GPU_TYPE_ET4000AX,
} gpu_type_t;

#define GPU_CAP_LINEAR_FB   (1u << 0)
#define GPU_CAP_ACCEL_2D    (1u << 1)
#define GPU_CAP_HW_CURSOR   (1u << 2)
#define GPU_CAP_VBE_BIOS    (1u << 3)

typedef struct {
    gpu_type_t type;
    char       name[32];
    pci_device_t pci;
    uint32_t   framebuffer_bar;
    uint32_t   framebuffer_base;
    uint32_t   framebuffer_size;
    uint32_t   mmio_bar;
    uint32_t   capabilities;
    uint16_t   framebuffer_width;
    uint16_t   framebuffer_height;
    uint32_t   framebuffer_pitch;
    uint8_t    framebuffer_bpp;
    volatile uint8_t* framebuffer_ptr;
} gpu_info_t;

void gpu_init(void);
const gpu_info_t* gpu_get_devices(size_t* count);
void gpu_log_summary(void);
void gpu_dump_details(void);
int  gpu_request_framebuffer_mode(uint16_t width, uint16_t height, uint8_t bpp);
void gpu_restore_text_mode(void);
void gpu_debug_probe(int scan_legacy);
int  gpu_manual_activate_et4000(uint16_t width, uint16_t height, uint8_t bpp);

#endif // DRIVERS_GPU_GPU_H
