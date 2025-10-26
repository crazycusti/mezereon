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
    GPU_TYPE_VGA,
    GPU_TYPE_AVGA2,
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

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
} gpu_mode_option_t;

void gpu_init(void);
const gpu_info_t* gpu_get_devices(size_t* count);
void gpu_log_summary(void);
void gpu_dump_details(void);
int  gpu_request_framebuffer_mode(uint16_t width, uint16_t height, uint8_t bpp);
void gpu_restore_text_mode(void);
void gpu_debug_probe(int scan_legacy);
int  gpu_manual_activate_et4000(uint16_t width, uint16_t height, uint8_t bpp);
void gpu_tseng_set_auto_enabled(int enabled);
int  gpu_tseng_get_auto_enabled(void);
void gpu_set_debug(int enabled);
int  gpu_get_debug(void);
void gpu_debug_log(const char* level, const char* msg);
void gpu_debug_log_hex(const char* level, const char* label, uint32_t value);
void gpu_set_last_error(const char* msg);
const char* gpu_get_last_error(void);
void gpu_set_last_mode(const char* name, uint16_t width, uint16_t height, uint8_t bpp);
void gpu_get_last_mode(char* out_name, size_t name_len, uint16_t* width, uint16_t* height, uint8_t* bpp);
void gpu_print_status(void);
int  gpu_force_activate(gpu_type_t type, uint16_t width, uint16_t height, uint8_t bpp);
size_t gpu_get_mode_catalog(const gpu_info_t* gpu,
                            gpu_mode_option_t* out_modes,
                            size_t capacity);

#endif // DRIVERS_GPU_GPU_H
